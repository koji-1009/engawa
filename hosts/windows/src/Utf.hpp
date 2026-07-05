#pragma once
// UTF-8 <-> UTF-16 conversion. The wire and filesystem are UTF-8 (contract §2, spec/commands/fs.md);
// the Win32 / WebView2 APIs are UTF-16. Everything round-trips through here.
#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

inline std::wstring ToWide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

inline std::string ToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

inline std::string ToUtf8(const wchar_t* w) { return w ? ToUtf8(std::wstring_view(w)) : std::string(); }

// Read an environment variable as UTF-8 (the two-call GetEnvironmentVariableW size/read dance in one
// place). Empty optional means the variable is absent — distinct from present-but-empty.
inline std::optional<std::string> EnvOpt(const wchar_t* key) {
    DWORD n = GetEnvironmentVariableW(key, nullptr, 0);
    if (n == 0) return std::nullopt;
    std::wstring buf(n, L'\0');
    DWORD got = GetEnvironmentVariableW(key, buf.data(), n);
    buf.resize(got);
    return ToUtf8(buf);
}
