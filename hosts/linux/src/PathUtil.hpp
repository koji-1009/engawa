#pragma once
// Filesystem helpers over std::filesystem, taking/returning UTF-8 (the wire + fs contract are UTF-8).
// On Linux a std::filesystem::path IS a UTF-8 char string, so P() and U8() are near-identities — no
// UTF-16 conversion (contrast the Windows host). Files::* centralize the atomic write (temp + rename)
// the fs and io paths both rely on (spec/commands/fs.md, §5a).
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "Json.hpp"

namespace fsys = std::filesystem;

inline fsys::path P(const std::string& u8) { return fsys::path(u8); }
inline std::string U8(const fsys::path& p) { return p.string(); }

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
// slip past the prefix check), then compared case-SENSITIVELY (Linux filesystems are case-sensitive)
// with a path-separator boundary.
inline bool isInside(const std::string& root, const std::string& full) {
    std::error_code ec;
    std::string r = fsys::absolute(P(root), ec).lexically_normal().string();
    while (!r.empty() && r.back() == '/') r.pop_back();
    std::string f = fsys::absolute(P(full), ec).lexically_normal().string();
    if (f.size() < r.size()) return false;
    if (f.compare(0, r.size(), r) != 0) return false;
    return f.size() == r.size() || f[r.size()] == '/';
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

// Atomic replace: write a sibling temp then rename over the target. POSIX rename(2) is atomic on one
// filesystem, so at any crash instant the target is the old bytes or the new — never partial
// (spec/commands/fs.md, §8).
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
