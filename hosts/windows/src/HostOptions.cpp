#include "HostOptions.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "Utf.hpp"

namespace {

bool flag(const wchar_t* key) {
    auto v = EnvOpt(key);
    return v && *v == "1";
}

std::string exeDir() {
    std::wstring buf(MAX_PATH, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    buf.resize(n);
    std::filesystem::path p(buf);
    return ToUtf8(p.parent_path().wstring());
}

std::string joinPath(const std::string& a, const std::string& b) {
    std::filesystem::path p(ToWide(a));
    p /= ToWide(b);
    return ToUtf8(p.wstring());
}

}  // namespace

HostOptions HostOptions::fromEnvironment() {
    HostOptions o;
    o.conformance = flag(L"ENGAWA_CONFORMANCE");
    o.autotest = flag(L"ENGAWA_AUTOTEST");
    o.autotestUpdate = EnvOpt(L"ENGAWA_AUTOTEST_UPDATE");
    o.appRoot = EnvOpt(L"ENGAWA_APP_ROOT").value_or("");
    o.dataRoot = EnvOpt(L"ENGAWA_DATA_ROOT").value_or("");
    o.bundleRoot = EnvOpt(L"ENGAWA_BUNDLE_ROOT").value_or("");
    o.shellJsPath = EnvOpt(L"ENGAWA_SHELL_JS").value_or("");
    o.trustRootB64 = EnvOpt(L"ENGAWA_TRUST_ROOT").value_or("");
    o.fakeEngineVersion = EnvOpt(L"ENGAWA_FAKE_ENGINE_VERSION");
    o.wipeStorage = flag(L"ENGAWA_WIPE_STORAGE");
    o.startUrl = EnvOpt(L"ENGAWA_START_URL");

    std::string dir = exeDir();
    if (o.appRoot.empty()) o.appRoot = joinPath(dir, "app");
    if (o.dataRoot.empty()) {
        // Per-app data root outside conformance (§path). appData/config/cache derive from it.
        auto appData = EnvOpt(L"APPDATA");
        o.dataRoot = appData ? joinPath(*appData, "Engawa") : joinPath(dir, "data");
    }
    if (o.shellJsPath.empty()) o.shellJsPath = joinPath(dir, "shell.js");
    if (o.bundleRoot.empty()) o.bundleRoot = dir;

    // A distributed app (double-clicked exe, no env) reads its update trust root (§7.1) from
    // trust-root.txt beside engawa.json when ENGAWA_TRUST_ROOT wasn't provided.
    if (o.trustRootB64.empty()) {
        std::ifstream f(std::filesystem::path(ToWide(o.bundleRoot)) / L"trust-root.txt", std::ios::binary);
        if (f) {
            std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
            if (a != std::string::npos) o.trustRootB64 = s.substr(a, b - a + 1);
        }
    }

    return o;
}
