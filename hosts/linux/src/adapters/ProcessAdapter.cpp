// process namespace (spec/commands/process.md). Streams are PULL: process.readable signals data
// availability (no bytes ride the event, §4.1); JS drains with process.read. The host buffers up to
// 8 MiB per stream then applies OS backpressure by pausing the pipe read. process.exit fires only
// after both streams are drained. Only manifest-declared, in-bundle sidecars run (§7.2). Children run
// in their own process group so kill reaps the whole tree.
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "PathUtil.hpp"
#include "adapters/Adapters.hpp"

namespace {

constexpr size_t kCap = 8 * 1024 * 1024;

struct StreamBuf {
    std::mutex gate;
    std::condition_variable resume;
    std::string data;         // backing store; the readable region is [off, data.size())
    size_t off = 0;           // read head — advancing it instead of erase(0,n) keeps draining O(n)
    bool eof = false;
    bool paused = false;
    bool everPaused = false;  // sticky: did backpressure ever pause this stream (__wasPaused)
    int readFd = -1;

    size_t size() const { return data.size() - off; }  // bytes available to read
    bool empty() const { return off >= data.size(); }
    const char* head() const { return data.data() + off; }
    void append(const char* p, size_t n) { data.append(p, n); }
    void consume(size_t n) {
        off += n;
        if (off >= data.size()) { data.clear(); off = 0; }
        else if (off >= 65536 && off * 2 >= data.size()) { data.erase(0, off); off = 0; }
    }
};

struct ProcState {
    pid_t pid = 0;  // also the child's process-group id (we setpgid) — killpg reaps the tree
    int stdinWrite = -1;
    StreamBuf out;
    StreamBuf err;
    std::atomic<bool> exited{false};
    std::atomic<bool> exitEmitted{false};
    int exitCode = 0;
};

// Longest byte length ≤ take whose bytes form only COMPLETE UTF-8 sequences (mirrors the mock host).
int validUtf8End(const char* buf, int take) {
    int end = take < 0 ? 0 : take;
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
    auto o = Files::readJsonObject(U8(P(bundleRoot) / "engawa.json"));
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
    // ---- spawn (§7.2 allowlist + in-bundle containment) -----------------------------------
    Json spawn(const Json& args) {
        std::string command = ja::reqString(args, "command");
        std::string exe = resolveSidecar(command);
        if (exe.empty()) throw EngawaError("EPERM", "not a declared in-bundle sidecar: " + command);

        // argv: the resolved executable (its shebang selects the interpreter) + declared args.
        std::vector<std::string> tokens{exe};
        if (args.is_object() && args.contains("args") && args["args"].is_array())
            for (auto& a : args["args"])
                if (a.is_string()) tokens.push_back(a.get<std::string>());
        std::vector<char*> argv;
        for (auto& t : tokens) argv.push_back(const_cast<char*>(t.c_str()));
        argv.push_back(nullptr);

        // O_CLOEXEC on every pipe fd: otherwise a spawn issued while another sidecar is alive inherits
        // that sidecar's parent-side fds (incl. its stdin write end) into the new child, so the first
        // sidecar never sees stdin EOF and hangs. The child dup2's the three ends it needs onto 0/1/2
        // (dup2 clears O_CLOEXEC on the target), and exec closes every other inherited pipe fd.
        int inPipe[2] = {-1, -1}, outPipe[2] = {-1, -1}, errPipe[2] = {-1, -1};
        auto closePipes = [&] {
            for (int* p : {inPipe, outPipe, errPipe})
                for (int i = 0; i < 2; i++)
                    if (p[i] >= 0) { close(p[i]); p[i] = -1; }
        };
        if (pipe2(inPipe, O_CLOEXEC) != 0 || pipe2(outPipe, O_CLOEXEC) != 0 || pipe2(errPipe, O_CLOEXEC) != 0) {
            closePipes();  // close any that succeeded before the failure (no fd leak on partial failure)
            throw EngawaError::io("pipe failed");
        }

        pid_t pid = ::fork();
        if (pid < 0) { closePipes(); throw EngawaError::io("fork failed"); }
        if (pid == 0) {
            // Child: only async-signal-safe calls before exec. New process group (kill reaps the tree),
            // wire the pipe ends onto stdio, cwd = bundle, then exec.
            setpgid(0, 0);
            dup2(inPipe[0], 0);
            dup2(outPipe[1], 1);
            dup2(errPipe[1], 2);
            close(inPipe[0]); close(inPipe[1]);
            close(outPipe[0]); close(outPipe[1]);
            close(errPipe[0]); close(errPipe[1]);
            if (chdir(bundleRoot_.c_str()) != 0) { /* fall through — exec will still try */ }
            execv(argv[0], argv.data());
            _exit(127);  // exec failed
        }
        // Parent: also set the child's pgid (race-free — whichever wins, the group is {pid}), close the
        // child ends, keep the parent ends.
        setpgid(pid, pid);
        close(inPipe[0]); close(outPipe[1]); close(errPipe[1]);

        auto st = std::make_unique<ProcState>();
        st->pid = pid;
        st->stdinWrite = inPipe[1];
        st->out.readFd = outPipe[0];
        st->err.readFd = errPipe[0];
        ProcState* raw = st.get();
        {
            std::lock_guard<std::mutex> lk(procsMu_);
            procs_[pid] = std::move(st);
        }

        startReader(raw, &raw->out, "stdout");
        startReader(raw, &raw->err, "stderr");
        std::thread([this, raw, pid] {
            int status = 0;
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) { /* retry */ }
            raw->exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1);
            raw->exited = true;
            maybeExit(raw, pid);
        }).detach();

        return Json{{"pid", static_cast<int>(pid)}};
    }

    // §7.2: the allowlist is the declared string; the resolved path must be a file inside the bundle.
    std::string resolveSidecar(const std::string& command) {
        if (!sidecars_.count(command)) return "";
        std::error_code ec;
        std::string cand = U8(fsys::absolute(P(bundleRoot_) / P(command), ec));
        if (!Files::isFile(cand)) return "";
        if (!Files::isInside(bundleRoot_, cand)) return "";  // §7.2 containment on the resolved path
        return cand;
    }

    // ---- stdin / read / kill --------------------------------------------------------------
    Json stdinWrite(const Json& args) {
        // Validate data (EINVAL) before the pid lookup (ESRCH) so error precedence matches the mock host.
        std::string data = ja::reqStringAllowEmpty(args, "data");
        ProcState* st = require(args);
        if (st->exitEmitted) return Json(nullptr);  // already exited: no-op
        if (st->stdinWrite < 0) throw EngawaError::io("stdin is closed");  // after stdinClose
        size_t total = 0;
        while (total < data.size()) {
            ssize_t n = ::write(st->stdinWrite, data.data() + total, data.size() - total);
            if (n < 0) {
                if (errno == EINTR) continue;
                throw EngawaError::io("stdin write failed");
            }
            total += static_cast<size_t>(n);
        }
        return Json(nullptr);
    }

    Json stdinClose(const Json& args) {
        ProcState* st = require(args);
        if (st->exitEmitted) return Json(nullptr);
        if (st->stdinWrite >= 0) { close(st->stdinWrite); st->stdinWrite = -1; }
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
        ::killpg(st->pid, SIGKILL);  // kill the whole process group (§ kill)
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
                ssize_t n = ::read(sb->readFd, tmp.data(), tmp.size());
                if (n < 0) { if (errno == EINTR) continue; break; }
                if (n == 0) break;  // EOF
                bool wasEmpty;
                {
                    std::lock_guard<std::mutex> lk(sb->gate);
                    wasEmpty = sb->empty();
                    sb->append(tmp.data(), static_cast<size_t>(n));
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
        // Reap fds: the process exited, both streams are EOF + drained, and exitEmitted now latches
        // every other op to a no-op — so nothing else will touch them. The map entry stays as a drained
        // tombstone (post-exit reads return eof, kill is a no-op).
        if (st->stdinWrite >= 0) { close(st->stdinWrite); st->stdinWrite = -1; }
        if (st->out.readFd >= 0) { close(st->out.readFd); st->out.readFd = -1; }
        if (st->err.readFd >= 0) { close(st->err.readFd); st->err.readFd = -1; }
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
