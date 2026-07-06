// process namespace (spec/commands/process.md). Streams are PULL: process.readable signals data
// availability (no bytes ride the event, §4.1); JS drains with process.read. The host buffers up to
// 8 MiB per stream then applies OS backpressure by pausing the pipe read. process.exit fires only
// after both streams are drained. Only manifest-declared, in-bundle sidecars run (§7.2).
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "PathUtil.hpp"
#include "Utf.hpp"
#include "adapters/Adapters.hpp"

namespace {

constexpr size_t kCap = 8 * 1024 * 1024;
const char* kExeExts[] = {".exe", ".com", ".bat", ".cmd"};

struct StreamBuf {
    std::mutex gate;
    std::condition_variable resume;
    std::string data;         // backing store; the readable region is [off, data.size())
    size_t off = 0;           // read head — advancing it instead of erase(0,n) keeps draining O(n)
    bool eof = false;
    bool paused = false;
    bool everPaused = false;  // sticky: did backpressure ever pause this stream (__wasPaused)
    HANDLE readEnd = nullptr;

    size_t size() const { return data.size() - off; }  // bytes available to read
    bool empty() const { return off >= data.size(); }
    const char* head() const { return data.data() + off; }
    void append(const char* p, size_t n) { data.append(p, n); }
    // Advance past consumed bytes; compact only occasionally so a full drain is amortized O(n), not
    // the O(n^2) that erase(0,n) on every small read would cost against the 8 MiB cap.
    void consume(size_t n) {
        off += n;
        if (off >= data.size()) { data.clear(); off = 0; }
        else if (off >= 65536 && off * 2 >= data.size()) { data.erase(0, off); off = 0; }
    }
};

struct ProcState {
    HANDLE hProcess = nullptr;
    HANDLE job = nullptr;  // whole-process-tree kill (a sidecar may itself spawn children)
    DWORD pid = 0;
    HANDLE stdinWrite = nullptr;
    StreamBuf out;
    StreamBuf err;
    std::atomic<bool> exited{false};
    std::atomic<bool> exitEmitted{false};
    int exitCode = 0;
};

// Longest byte length ≤ take whose bytes form only COMPLETE UTF-8 sequences — so a read never emits a
// partial character, including when the tail is an incomplete multibyte sequence still awaiting
// continuation bytes on the pipe (mirrors the mock host).
int validUtf8End(const char* buf, int take) {
    int end = take < 0 ? 0 : take;  // caller passes take <= available bytes
    while (end > 0) {
        int i = end - 1;
        while (i > 0 && ((unsigned char)buf[i] & 0xc0) == 0x80 && end - i < 4) i--;
        unsigned char lead = buf[i];
        int seqLen;
        if ((lead & 0x80) == 0) seqLen = 1;
        else if ((lead & 0xe0) == 0xc0) seqLen = 2;
        else if ((lead & 0xf0) == 0xe0) seqLen = 3;
        else if ((lead & 0xf8) == 0xf0) seqLen = 4;
        else seqLen = 1;
        if (i + seqLen <= end) break;
        end = i;
    }
    return end;
}

std::unordered_set<std::string> loadSidecars(const std::string& bundleRoot) {
    std::unordered_set<std::string> set;
    auto o = Files::readJsonObject(U8(P(bundleRoot) / L"engawa.json"));
    if (o && o->contains("sidecars") && (*o)["sidecars"].is_array())
        for (auto& s : (*o)["sidecars"])
            if (s.is_string()) set.insert(s.get<std::string>());
    return set;
}

class ProcessAdapter : public IAdapter {
public:
    ProcessAdapter(IEventEmitter* emitter, const HostOptions& opts)
        : emitter_(emitter), conformance_(opts.conformance) {
        std::error_code ec;
        bundleRoot_ = U8(fsys::absolute(P(opts.bundleRoot), ec));
        sidecars_ = loadSidecars(bundleRoot_);
    }

    std::string ns() const override { return "process"; }

    Json handle(const std::string& command, const Json& args) override {
        if (command == "spawn") return spawn(args);
        if (command == "stdinWrite") return stdinWrite(args);
        if (command == "stdinClose") return stdinClose(args);
        if (command == "read") return read(args);
        if (command == "kill") return kill(args);
        if (command == "__wasPaused" && conformance_) return wasPaused(args);
        throw EngawaError::nosys("process." + command);
    }

private:
    // ---- spawn (§7.2 allowlist + platform executable resolution) --------------------------
    Json spawn(const Json& args) {
        std::string command = ja::reqString(args, "command");
        bool isBatch = false;
        std::string exe = resolveSidecar(command, isBatch);
        if (exe.empty()) throw EngawaError("EPERM", "not a declared in-bundle sidecar: " + command);

        std::vector<std::string> tokens;
        if (isBatch) {
            tokens.push_back(EnvOpt(L"ComSpec").value_or("cmd.exe"));
            tokens.push_back("/c");
            tokens.push_back(exe);
        } else {
            tokens.push_back(exe);
        }
        if (args.is_object() && args.contains("args") && args["args"].is_array())
            for (auto& a : args["args"])
                if (a.is_string()) tokens.push_back(a.get<std::string>());

        std::string cmdline;
        for (size_t i = 0; i < tokens.size(); i++) {
            if (i) cmdline.push_back(' ');
            cmdline += quoteArg(tokens[i]);
        }

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
        if (!CreatePipe(&inRead, &inWrite, &sa, 0) || !CreatePipe(&outRead, &outWrite, &sa, 0) ||
            !CreatePipe(&errRead, &errWrite, &sa, 0))
            throw EngawaError::io("CreatePipe failed");
        SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inRead;
        si.hStdOutput = outWrite;
        si.hStdError = errWrite;
        PROCESS_INFORMATION pi{};
        std::wstring wcmd = ToWide(cmdline);
        std::vector<wchar_t> mutableCmd(wcmd.begin(), wcmd.end());
        mutableCmd.push_back(L'\0');
        std::wstring cwd = ToWide(bundleRoot_);

        // Spawn suspended so the process can be placed in a job (which owns its whole child tree)
        // BEFORE it runs — the sidecar image is often a launcher (cmd.exe → node) whose real work is a
        // child, and kill must reap the tree, not orphan node holding the stdout pipe open.
        BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, cwd.c_str(), &si, &pi);
        CloseHandle(inRead);
        CloseHandle(outWrite);
        CloseHandle(errWrite);
        if (!ok) {
            CloseHandle(inWrite);
            CloseHandle(outRead);
            CloseHandle(errRead);
            throw EngawaError::io("CreateProcess failed");
        }

        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
            AssignProcessToJobObject(job, pi.hProcess);
        }
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);

        int pid = static_cast<int>(pi.dwProcessId);
        auto st = std::make_unique<ProcState>();
        st->hProcess = pi.hProcess;
        st->job = job;
        st->pid = pi.dwProcessId;
        st->stdinWrite = inWrite;
        st->out.readEnd = outRead;
        st->err.readEnd = errRead;
        ProcState* raw = st.get();
        {
            std::lock_guard<std::mutex> lk(procsMu_);
            procs_[pid] = std::move(st);
        }

        startReader(raw, &raw->out, "stdout");
        startReader(raw, &raw->err, "stderr");
        std::thread([this, raw, pid] {
            WaitForSingleObject(raw->hProcess, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(raw->hProcess, &code);
            raw->exitCode = static_cast<int>(code);
            raw->exited = true;
            maybeExit(raw, pid);
        }).detach();

        return Json{{"pid", pid}};
    }

    // Windows-runnable extensions; the bare, extension-less declared path is skipped in favour of an
    // extension candidate (the repo ships both the Unix script and a .cmd).
    std::string resolveSidecar(const std::string& command, bool& isBatch) {
        isBatch = false;
        if (!sidecars_.count(command)) return "";  // allowlist is on the declared string
        std::string basePath = U8(fsys::absolute(P(bundleRoot_) / P(command)));

        std::vector<std::string> candidates;
        std::string declaredExt = lowerExt(command);
        bool declaredRunnable = false;
        for (auto* e : kExeExts)
            if (declaredExt == e) declaredRunnable = true;
        if (declaredRunnable) candidates.push_back(basePath);
        else
            for (auto* e : kExeExts) candidates.push_back(basePath + e);

        for (auto& cand : candidates) {
            if (!Files::isFile(cand)) continue;
            if (!Files::isInside(bundleRoot_, cand)) return "";  // §7.2 containment on the resolved path
            std::string ext = lowerExt(cand);
            isBatch = (ext == ".bat" || ext == ".cmd");
            return cand;
        }
        return "";
    }

    // ---- stdin / read / kill --------------------------------------------------------------
    Json stdinWrite(const Json& args) {
        // Validate data (EINVAL) before the pid lookup (ESRCH) so error precedence matches the mock host.
        std::string data = ja::reqStringAllowEmpty(args, "data");
        ProcState* st = require(args);
        if (st->exitEmitted) return Json(nullptr);  // already exited: no-op
        if (!st->stdinWrite) throw EngawaError::io("stdin is closed");  // after stdinClose
        DWORD written = 0;
        if (!data.empty() && !WriteFile(st->stdinWrite, data.data(), (DWORD)data.size(), &written, nullptr))
            throw EngawaError::io("stdin write failed");
        return Json(nullptr);
    }

    Json stdinClose(const Json& args) {
        ProcState* st = require(args);
        if (st->exitEmitted) return Json(nullptr);
        if (st->stdinWrite) { CloseHandle(st->stdinWrite); st->stdinWrite = nullptr; }
        return Json(nullptr);
    }

    Json read(const Json& args) {
        ProcState* st = require(args);
        auto streamArg = ja::optString(args, "stream");
        if (streamArg && *streamArg != "stdout" && *streamArg != "stderr")
            throw EngawaError::invalid("bad stream");
        int maxBytes = 65536;
        if (args.is_object() && args.contains("maxBytes") && !args["maxBytes"].is_null()) {
            if (!args["maxBytes"].is_number()) throw EngawaError::invalid("bad maxBytes");
            double mb = args["maxBytes"].get<double>();
            if (mb < 0) throw EngawaError::invalid("bad maxBytes");
            // mb >= 0 here, but still may exceed INT_MAX (e.g. 1e300); static_cast<int> of an
            // out-of-range double is UB, so clamp before the cast.
            if (mb > static_cast<double>(INT_MAX)) mb = static_cast<double>(INT_MAX);
            maxBytes = static_cast<int>(mb);
        }
        StreamBuf& which = (streamArg && *streamArg == "stderr") ? st->err : st->out;

        std::string data;
        bool eof;
        {
            std::lock_guard<std::mutex> lk(which.gate);
            int avail = static_cast<int>(which.size());
            int take = maxBytes < 0 ? 0 : (maxBytes > avail ? avail : maxBytes);
            int end = validUtf8End(which.head(), take);
            // Forward progress (§4.1): return a whole buffered character even if maxBytes was too small.
            if (end == 0 && !which.empty())
                end = validUtf8End(which.head(), avail < 4 ? avail : 4);
            data.assign(which.head(), static_cast<size_t>(end));
            which.consume(static_cast<size_t>(end));
            if (which.paused && !which.eof && which.size() < kCap) {
                which.paused = false;
                which.resume.notify_all();  // lift backpressure
            }
            eof = which.eof && which.empty();
        }
        maybeExit(st, st->pid);
        return Json{{"data", data}, {"eof", eof}};
    }

    Json kill(const Json& args) {
        ProcState* st = require(args);
        if (st->exitEmitted) return Json(nullptr);  // already exited: no-op, not ESRCH
        if (st->job) TerminateJobObject(st->job, 1);  // kill the whole tree (§ kill)
        else TerminateProcess(st->hProcess, 1);
        return Json(nullptr);
    }

    Json wasPaused(const Json& args) {
        ProcState* st = require(args);
        auto streamArg = ja::optString(args, "stream");
        StreamBuf& which = (streamArg && *streamArg == "stderr") ? st->err : st->out;
        std::lock_guard<std::mutex> lk(which.gate);
        return which.everPaused;
    }

    ProcState* require(const Json& args) {
        int pid = ja::reqInt(args, "pid");
        std::lock_guard<std::mutex> lk(procsMu_);
        auto it = procs_.find(pid);
        if (it != procs_.end()) return it->second.get();  // exited procs stay mapped (post-exit no-ops)
        throw EngawaError("ESRCH", "no such process: " + std::to_string(pid));
    }

    // ---- stream pump ----------------------------------------------------------------------
    void startReader(ProcState* st, StreamBuf* sb, const char* which) {
        std::string whichStr = which;
        std::thread([this, st, sb, whichStr] {
            std::vector<char> tmp(65536);
            for (;;) {
                {
                    std::unique_lock<std::mutex> lk(sb->gate);
                    sb->resume.wait(lk, [sb] { return !sb->paused; });  // block while paused (backpressure)
                }
                DWORD n = 0;
                if (!ReadFile(sb->readEnd, tmp.data(), (DWORD)tmp.size(), &n, nullptr) || n == 0) break;
                bool wasEmpty;
                {
                    std::lock_guard<std::mutex> lk(sb->gate);
                    wasEmpty = sb->empty();
                    sb->append(tmp.data(), n);
                    if (sb->size() >= kCap) { sb->paused = true; sb->everPaused = true; }
                }
                // §4.1: level-triggered readable when a drained stream goes non-empty; {pid,stream}
                // ARE the signal — no data bytes ride the event.
                if (wasEmpty) emitter_->emit("process.readable", Json{{"pid", (int)st->pid}, {"stream", whichStr}});
            }
            { std::lock_guard<std::mutex> lk(sb->gate); sb->eof = true; }
            maybeExit(st, (int)st->pid);
        }).detach();
    }

    // exit fires only after the process exited AND both streams are fully drained (§4.1).
    void maybeExit(ProcState* st, int pid) {
        if (!st->exited || st->exitEmitted) return;
        bool drained;
        {
            std::scoped_lock lk(st->out.gate, st->err.gate);
            drained = st->out.eof && st->err.eof && st->out.empty() && st->err.empty();
        }
        if (!drained) return;
        bool expected = false;
        if (!st->exitEmitted.compare_exchange_strong(expected, true)) return;  // emit exactly once
        // Reap handles: the process has exited, both streams are EOF + drained, and exitEmitted now
        // latches every other op (read/kill/stdin*) to a no-op — so nothing else will touch them. The
        // map entry stays as a drained tombstone (post-exit reads return eof, kill is a no-op). This is
        // the only place the process/job/pipe handles are closed, so each spawn is fully reclaimed.
        if (st->stdinWrite) { CloseHandle(st->stdinWrite); st->stdinWrite = nullptr; }
        if (st->out.readEnd) { CloseHandle(st->out.readEnd); st->out.readEnd = nullptr; }
        if (st->err.readEnd) { CloseHandle(st->err.readEnd); st->err.readEnd = nullptr; }
        if (st->job) { CloseHandle(st->job); st->job = nullptr; }
        if (st->hProcess) { CloseHandle(st->hProcess); st->hProcess = nullptr; }
        emitter_->emit("process.exit", Json{{"pid", pid}, {"code", st->exitCode}});
    }

    IEventEmitter* emitter_;
    bool conformance_;
    std::string bundleRoot_;
    std::unordered_set<std::string> sidecars_;
    std::mutex procsMu_;
    std::unordered_map<int, std::unique_ptr<ProcState>> procs_;
};

}  // namespace

std::unique_ptr<IAdapter> makeProcessAdapter(IEventEmitter* emitter, const HostOptions& opts) {
    return std::make_unique<ProcessAdapter>(emitter, opts);
}
