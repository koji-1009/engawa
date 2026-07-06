#include "HostOptions.hpp"

#include <limits.h>
#include <unistd.h>

#include <filesystem>

#include "Utf.hpp"

namespace {

bool flag(const char* key) {
    auto v = EnvOpt(key);
    return v && *v == "1";
}

// The directory holding the running executable, via /proc/self/exe (the Linux twin of
// GetModuleFileName). Assets/shell.js/data default relative to it so a launched binary just works.
std::string exeDir() {
    char buf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf));
    if (n <= 0) return ".";
    std::filesystem::path p(std::string(buf, static_cast<size_t>(n)));
    return p.parent_path().string();
}

std::string joinPath(const std::string& a, const std::string& b) {
    return (std::filesystem::path(a) / b).string();
}

}  // namespace

HostOptions HostOptions::fromEnvironment() {
    HostOptions o;
    o.conformance = flag("ENGAWA_CONFORMANCE");
    o.autotest = flag("ENGAWA_AUTOTEST");
    o.autotestUpdate = EnvOpt("ENGAWA_AUTOTEST_UPDATE");
    o.appRoot = EnvOpt("ENGAWA_APP_ROOT").value_or("");
    o.dataRoot = EnvOpt("ENGAWA_DATA_ROOT").value_or("");
    o.bundleRoot = EnvOpt("ENGAWA_BUNDLE_ROOT").value_or("");
    o.shellJsPath = EnvOpt("ENGAWA_SHELL_JS").value_or("");
    o.trustRootB64 = EnvOpt("ENGAWA_TRUST_ROOT").value_or("");
    o.fakeEngineVersion = EnvOpt("ENGAWA_FAKE_ENGINE_VERSION");
    o.wipeStorage = flag("ENGAWA_WIPE_STORAGE");
    o.startUrl = EnvOpt("ENGAWA_START_URL");

    std::string dir = exeDir();
    if (o.appRoot.empty()) o.appRoot = joinPath(dir, "app");
    if (o.dataRoot.empty()) {
        // Per-app data root outside conformance (§path). appData/config/cache derive from it. Follows
        // the XDG base-directory spec: $XDG_DATA_HOME, else ~/.local/share.
        auto xdg = EnvOpt("XDG_DATA_HOME");
        if (xdg && !xdg->empty()) {
            o.dataRoot = joinPath(*xdg, "engawa");
        } else if (auto home = EnvOpt("HOME"); home && !home->empty()) {
            o.dataRoot = joinPath(joinPath(*home, ".local/share"), "engawa");
        } else {
            o.dataRoot = joinPath(dir, "data");
        }
    }
    if (o.shellJsPath.empty()) o.shellJsPath = joinPath(dir, "shell.js");
    if (o.bundleRoot.empty()) o.bundleRoot = dir;

    // trustRootB64 holds only the environment-provided root (dev/conformance, §7.1). A distributable's
    // real trust root is compiled into the binary — bakedTrustRoot() in the generated Compose — and is
    // resolved in main(); it is deliberately NOT read from a file beside the executable, which could be
    // swapped to authorize a malicious update.
    return o;
}
