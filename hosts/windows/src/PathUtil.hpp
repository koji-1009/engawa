#pragma once
// Filesystem helpers over std::filesystem, taking/returning UTF-8 (the wire + fs contract are UTF-8).
// P() lifts a UTF-8 string to a native path; U8() lowers a path back. Files::* centralize the atomic
// write (temp + rename) the fs and io paths both rely on (spec/commands/fs.md, §5a).
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "Json.hpp"
#include "Utf.hpp"

namespace fsys = std::filesystem;

inline fsys::path P(const std::string& u8) { return fsys::path(ToWide(u8)); }
inline std::string U8(const fsys::path& p) { return ToUtf8(p.wstring()); }

namespace Files {

inline bool exists(const std::string& p) {
    std::error_code ec;
    return fsys::exists(P(p), ec);
}
inline bool isDir(const std::string& p) {
    std::error_code ec;
    return fsys::is_directory(P(p), ec);
}
inline bool isFile(const std::string& p) {
    std::error_code ec;
    return fsys::is_regular_file(P(p), ec);
}

inline bool readAllBytes(const std::string& path, std::string& out) {
    std::ifstream f(P(path), std::ios::binary);
    if (!f) return false;
    // Size the buffer to the file and read straight into it — one allocation, no stringstream copy
    // (this path also backs update-payload hashing, where the extra copy tripled peak memory).
    f.seekg(0, std::ios::end);
    std::streamoff len = f.tellg();
    if (len < 0) {  // non-seekable (rare); fall back to a growing read
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(len));
    if (len > 0) f.read(out.data(), len);
    out.resize(static_cast<size_t>(f.gcount()));
    return true;
}

// Traversal / containment guard (spec/assets.md 403, §7.2 sidecar containment): is `full` at or
// beneath `root`? BOTH operands are made absolute and lexically-normalized (so a `..` segment cannot
// slip past the prefix check), then compared case-insensitively with a path-separator boundary.
inline bool isInside(const std::string& root, const std::string& full) {
    std::error_code ec;
    std::wstring r = fsys::absolute(P(root), ec).lexically_normal().wstring();
    while (!r.empty() && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
    std::wstring f = fsys::absolute(P(full), ec).lexically_normal().wstring();
    if (f.size() < r.size()) return false;
    if (CompareStringOrdinal(f.c_str(), static_cast<int>(r.size()), r.c_str(), static_cast<int>(r.size()), TRUE) != CSTR_EQUAL)
        return false;
    return f.size() == r.size() || f[r.size()] == L'\\' || f[r.size()] == L'/';
}

// Read a small JSON control/manifest file. Empty optional if the file is missing, unreadable,
// malformed, or not a JSON object — the tolerant-parse guard (allow_exceptions=false + object-check)
// the manifest / sidecar / update-slot readers all share.
inline std::optional<Json> readJsonObject(const std::string& path) {
    std::string text;
    if (!readAllBytes(path, text)) return std::nullopt;
    Json o = Json::parse(text, nullptr, false);
    if (o.is_discarded() || !o.is_object()) return std::nullopt;
    return o;
}

// Atomic replace: write a sibling temp then rename over the target. MSVC's std::filesystem::rename
// maps to MoveFileExW(REPLACE_EXISTING), so the swap is atomic on one volume (spec/commands/fs.md, §8).
inline void writeAllBytesAtomic(const std::string& path, const std::string& bytes) {
    std::string tmp = path + ".engawa-tmp";
    {
        std::ofstream f(P(tmp), std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("cannot open temp for write: " + tmp);
        if (!bytes.empty()) f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!f) throw std::runtime_error("write failed: " + tmp);
    }
    std::error_code ec;
    fsys::rename(P(tmp), P(path), ec);
    if (ec) {
        fsys::remove(P(tmp), ec);
        throw std::runtime_error("rename failed: " + path);
    }
}

}  // namespace Files

// Quote one argument for a Win32 command line (CommandLineToArgvW rules: double interior quotes'
// preceding backslashes, double a run of trailing backslashes before the closing quote). Shared by
// every child-process launch in the host so quoting is identical across them.
inline std::string quoteArg(const std::string& s) {
    if (!s.empty() && s.find_first_of(" \t\"") == std::string::npos) return s;
    std::string out = "\"";
    size_t backslashes = 0;
    for (char c : s) {
        if (c == '\\') { backslashes++; out.push_back(c); continue; }
        if (c == '"') { out.append(backslashes + 1, '\\'); out.push_back('"'); backslashes = 0; continue; }
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes, '\\');  // double trailing backslashes before the closing quote
    out.push_back('"');
    return out;
}

// Lowercased file extension of a path, including the leading dot (e.g. ".js"); empty if the last
// path segment has no dot.
inline std::string lowerExt(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string ext = path.substr(dot);
    for (char& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return ext;
}
