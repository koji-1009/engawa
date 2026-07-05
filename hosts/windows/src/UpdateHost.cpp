#include "UpdateHost.hpp"

#include <windows.h>

#include <vector>

#include "Crypto.hpp"
#include "PathUtil.hpp"
#include "Utf.hpp"

namespace {

std::optional<std::string> readTextTrim(const std::string& path) {
    std::string s;
    if (!Files::exists(path) || !Files::readAllBytes(path, s)) return std::nullopt;
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    return s.substr(a, b - a + 1);
}

void del(const std::string& p) {
    std::error_code ec;
    fsys::remove(P(p), ec);
}

// Recursive copy of a directory tree (seed slot A from the initial asset tree).
void copyDir(const std::string& src, const std::string& dst) {
    std::error_code ec;
    fsys::create_directories(P(dst), ec);
    fsys::copy(P(src), P(dst),
               fsys::copy_options::recursive | fsys::copy_options::overwrite_existing, ec);
}

// Extract a tar payload into targetDir using the system bsdtar (System32\tar.exe) — the same tool
// the suite/gate create the archive with (GNU tar misreads C: drive-letter paths). tar is a Windows
// system component, so nothing extra ships with the app.
void extractTar(const std::string& payload, const std::string& targetDir) {
    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    // Quote each argument with the shared quoteArg (same routine ProcessAdapter uses) so a payload or
    // slot path containing a space/quote/trailing backslash produces a well-formed command line.
    std::string tarExe = ToUtf8(std::wstring(sysDir)) + "\\tar.exe";
    std::string cmd = quoteArg(tarExe) + " -xf " + quoteArg(payload) + " -C " + quoteArg(targetDir);
    std::wstring wcmd = ToWide(cmd);
    std::vector<wchar_t> mutableCmd(wcmd.begin(), wcmd.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi))
        throw EngawaError::io("failed to launch tar for payload extraction");
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (code != 0) throw EngawaError::io("tar extraction failed");
}

}  // namespace

UpdateHost::UpdateHost(const std::string& dataRoot, const std::string& appSeedRoot,
                       const std::string& initialVersion, const std::string& trustRootB64)
    : trustRootB64_(trustRootB64) {
    root_ = U8(P(dataRoot) / L"engawa");
    slots_ = U8(P(root_) / L"slots");
    initialize(appSeedRoot, initialVersion);
}

std::string UpdateHost::liveRoot() const { return slotDir(bootingSlot_); }

std::string UpdateHost::slotDir(const std::string& s) const { return U8(P(slots_) / ToWide(s)); }
std::string UpdateHost::currentFile() const { return U8(P(root_) / L"current"); }
std::string UpdateHost::pendingFile() const { return U8(P(root_) / L"pending"); }
std::string UpdateHost::healthFile() const { return U8(P(root_) / L"health"); }
std::string UpdateHost::versionFile() const { return U8(P(root_) / L"version"); }

void UpdateHost::initialize(const std::string& appSeedRoot, const std::string& initialVersion) {
    std::error_code ec;
    fsys::create_directories(P(slotDir("a")), ec);
    fsys::create_directories(P(slotDir("b")), ec);

    if (!Files::exists(currentFile())) {
        // Seed slot A from the initial asset tree, then it is live.
        copyDir(appSeedRoot, slotDir("a"));
        Files::writeAllBytesAtomic(currentFile(), "a");
        Files::writeAllBytesAtomic(versionFile(), initialVersion);
    }
    current_ = readTextTrim(currentFile()).value_or("a");
    bootDecision();
}

Json UpdateHost::bootDecision() {
    auto pending = readPending();
    if (!pending) {
        bootingSlot_ = current_;
    } else {
        int attempts = 0;
        if (auto h = readHealth(); h && h->contains("attempts") && (*h)["attempts"].is_number())
            attempts = (*h)["attempts"].get<int>();
        attempts += 1;
        if (attempts <= 2) {
            bootingSlot_ = pending->slot;
            Json h = {{"bootingSlot", bootingSlot_}, {"attempts", attempts}};
            Files::writeAllBytesAtomic(healthFile(), h.dump());
        } else {
            // Two unconfirmed launches → the payload is verified-but-broken. Discard and roll back.
            del(pendingFile());
            del(healthFile());
            bootingSlot_ = current_;
        }
    }
    return status();
}

Json UpdateHost::status() {
    auto pending = readPending();
    Json o;
    o["currentSlot"] = current_;
    o["bootingSlot"] = bootingSlot_;
    o["version"] = readTextTrim(versionFile()).value_or("0.0.0");
    o["hasPending"] = pending.has_value();
    o["pendingSlot"] = pending ? Json(pending->slot) : Json(nullptr);
    return o;
}

Json UpdateHost::stageAppUpdate(const std::string& payloadPath, const std::string& hash,
                                const std::string& signature, const std::string& version) {
    if (!Files::exists(payloadPath)) throw EngawaError::noent("no such payload: " + payloadPath);

    std::string bytes;
    if (!Files::readAllBytes(payloadPath, bytes)) throw EngawaError::io("cannot read payload");
    std::string digest = Crypto::sha256(bytes);

    if (_stricmp(Crypto::toHex(digest).c_str(), hash.c_str()) != 0)
        throw EngawaError("EHASH", "payload hash does not match the manifest");

    if (!verifySignature(digest, signature))
        throw EngawaError("ESIGNATURE", "payload signature did not verify against the trust root");

    // Unpack into the non-live slot — other(bootingSlot), NOT other(current): when a pending slot is
    // booted-but-unconfirmed (bootingSlot != current), the live root is the booting slot, so staging
    // must target the other one or it would wipe what app:// is serving (update.test).
    std::string target = other(bootingSlot_);
    std::string targetDir = slotDir(target);
    std::error_code ec;
    fsys::remove_all(P(targetDir), ec);
    fsys::create_directories(P(targetDir), ec);
    extractTar(payloadPath, targetDir);

    // A freshly staged payload gets a fresh rollback budget (§8: 2 launch attempts).
    del(healthFile());

    // The single atomic commit point (§8): at any crash instant the state is "pre-update" or
    // "adoption reserved", never in between.
    Json p = {{"slot", target}, {"version", version}};
    Files::writeAllBytesAtomic(pendingFile(), p.dump());
    return Json{{"staged", true}};
}

void UpdateHost::confirmBoot() {
    auto pending = readPending();
    if (!pending || pending->slot != bootingSlot_) return;  // nothing to adopt
    current_ = pending->slot;
    Files::writeAllBytesAtomic(currentFile(), current_);
    Files::writeAllBytesAtomic(versionFile(), pending->version);
    del(pendingFile());
    del(healthFile());
}

bool UpdateHost::verifySignature(const std::string& digest, const std::string& signatureB64) {
    if (trustRootB64_.empty()) return false;  // no embedded key → nothing verifies
    std::string pub = Crypto::base64Decode(trustRootB64_);
    std::string sig = Crypto::base64Decode(signatureB64);
    if (pub.size() != 32) return false;
    // Node signs the sha256 digest directly (PureEdDSA).
    return Crypto::ed25519Verify(pub, digest, sig);
}

std::optional<UpdateHost::Pending> UpdateHost::readPending() const {
    auto o = Files::readJsonObject(pendingFile());
    if (!o || !o->contains("slot") || !(*o)["slot"].is_string()) return std::nullopt;
    Pending p;
    p.slot = (*o)["slot"].get<std::string>();
    p.version = o->contains("version") && (*o)["version"].is_string() ? (*o)["version"].get<std::string>() : "0.0.0";
    return p;
}

std::optional<Json> UpdateHost::readHealth() const { return Files::readJsonObject(healthFile()); }
