// fs namespace (spec/commands/fs.md) — text only; binary rides app://io (§5a) via openRead/openWrite,
// which mint tokens here and hand the bytes to the scheme handler. Paths are absolute and not
// sandboxed in v1 (contract §7). All I/O is UTF-8 and round-trips byte-for-byte.
#include <windows.h>

#include "PathUtil.hpp"
#include "Utf.hpp"
#include "adapters/Adapters.hpp"

namespace {

// A missing/empty path is EINVAL; so is a relative one — a GUI app has no well-defined cwd, so paths
// must be absolute (spec/commands/fs.md). std::filesystem::is_absolute requires both a root-name and
// a root-directory on Windows, which rejects drive-relative "C:foo" and rootless "\foo" too.
std::string reqPath(const Json& args) {
    std::string p = ja::reqString(args, "path");
    if (!P(p).is_absolute()) throw EngawaError::invalid("path must be absolute: " + p);
    return p;
}

std::string fullPath(const std::string& p) {
    std::error_code ec;
    return U8(fsys::absolute(P(p), ec));
}

bool isValidUtf8(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = s[i];
        size_t len;
        if (c < 0x80) len = 1;
        else if ((c & 0xe0) == 0xc0) { len = 2; if (c < 0xc2) return false; }
        else if ((c & 0xf0) == 0xe0) len = 3;
        else if ((c & 0xf8) == 0xf0) { len = 4; if (c > 0xf4) return false; }
        else return false;
        if (i + len > n) return false;
        for (size_t k = 1; k < len; k++)
            if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80) return false;
        i += len;
    }
    return true;
}

double modifiedMs(const std::string& p) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(ToWide(p).c_str(), GetFileExInfoStandard, &fad)) return 0;
    ULARGE_INTEGER t;
    t.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    t.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    // FILETIME is 100 ns ticks since 1601-01-01; Unix epoch is 11644473600 s later.
    return static_cast<double>(t.QuadPart / 10000ULL) - 11644473600000.0;
}

class FsAdapter : public IAdapter {
public:
    explicit FsAdapter(IoChannel& io) : io_(io) {}

    std::string ns() const override { return "fs"; }

    Json handle(const std::string& command, const Json& args) override {
        if (command == "readTextFile") return readTextFile(reqPath(args));
        if (command == "writeTextFile") return writeTextFile(reqPath(args), ja::reqStringAllowEmpty(args, "contents"));
        if (command == "exists") { std::string p = reqPath(args); return Files::exists(p); }
        if (command == "mkdir") return mkdir(reqPath(args), ja::optBool(args, "recursive"));
        if (command == "remove") return remove(reqPath(args), ja::optBool(args, "recursive"));
        if (command == "readDir") return readDir(reqPath(args));
        if (command == "stat") return stat(reqPath(args));
        if (command == "openWrite") return openWrite(reqPath(args));
        if (command == "openRead") return openRead(reqPath(args));
        throw EngawaError::nosys("fs." + command);
    }

private:
    static Json readTextFile(const std::string& p) {
        if (Files::isDir(p)) throw EngawaError("EISDIR", "is a directory: " + p);
        if (!Files::isFile(p)) throw EngawaError::noent("no such file: " + p);
        std::string bytes;
        if (!Files::readAllBytes(p, bytes)) throw EngawaError::io("cannot read: " + p);
        if (!isValidUtf8(bytes)) throw EngawaError::io("file is not valid UTF-8: " + p);
        return bytes;
    }

    static Json writeTextFile(const std::string& p, const std::string& contents) {
        std::error_code ec;
        fsys::path parent = P(fullPath(p)).parent_path();
        if (!fsys::is_directory(parent, ec))
            throw EngawaError::noent("parent directory does not exist: " + U8(parent));
        try { Files::writeAllBytesAtomic(p, contents); }  // atomic: temp + rename
        catch (const std::exception& e) { throw EngawaError::io(e.what()); }
        return Json(nullptr);
    }

    static Json mkdir(const std::string& p, bool recursive) {
        std::error_code ec;
        if (Files::exists(p)) {
            if (recursive) return Json(nullptr);
            throw EngawaError("EEXIST", "already exists: " + p);
        }
        fsys::path parent = P(fullPath(p)).parent_path();
        if (!recursive && !fsys::is_directory(parent, ec))
            throw EngawaError::noent("parent directory does not exist: " + U8(parent));
        bool ok = recursive ? fsys::create_directories(P(p), ec) : fsys::create_directory(P(p), ec);
        if (!ok || ec) throw EngawaError::io(ec ? ec.message() : "mkdir failed: " + p);
        return Json(nullptr);
    }

    static Json remove(const std::string& p, bool recursive) {
        std::error_code ec;
        bool isDir = Files::isDir(p);
        if (!isDir && !Files::isFile(p)) throw EngawaError::noent("no such path: " + p);
        if (isDir) {
            if (!recursive && !fsys::is_empty(P(p), ec))
                throw EngawaError("ENOTEMPTY", "directory not empty: " + p);
            if (recursive) fsys::remove_all(P(p), ec);
            else fsys::remove(P(p), ec);
        } else {
            fsys::remove(P(p), ec);
        }
        if (ec) throw EngawaError::io(ec.message());
        return Json(nullptr);
    }

    static Json readDir(const std::string& p) {
        if (!Files::isDir(p)) {
            if (Files::isFile(p)) throw EngawaError("ENOTDIR", "not a directory: " + p);
            throw EngawaError::noent("no such path: " + p);
        }
        Json arr = Json::array();
        std::error_code ec;
        for (auto& entry : fsys::directory_iterator(P(p), ec)) {
            arr.push_back(Json{{"name", U8(entry.path().filename())},
                               {"isDirectory", entry.is_directory(ec)}});
        }
        return arr;
    }

    static Json stat(const std::string& p) {
        std::error_code ec;
        if (Files::isDir(p))
            return Json{{"type", "directory"}, {"size", 0}, {"modified", modifiedMs(p)}};
        if (Files::isFile(p)) {
            auto size = static_cast<double>(fsys::file_size(P(p), ec));
            return Json{{"type", "file"}, {"size", size}, {"modified", modifiedMs(p)}};
        }
        throw EngawaError::noent("no such path: " + p);
    }

    Json openWrite(const std::string& p) {
        std::error_code ec;
        fsys::path parent = P(fullPath(p)).parent_path();
        if (!fsys::is_directory(parent, ec))
            throw EngawaError::noent("parent directory does not exist: " + U8(parent));
        return Json{{"url", "app://io/" + io_.mint(fullPath(p), true)}};
    }

    Json openRead(const std::string& p) {
        if (Files::isDir(p)) throw EngawaError("EISDIR", "is a directory: " + p);
        if (!Files::isFile(p)) throw EngawaError::noent("no such file: " + p);
        return Json{{"url", "app://io/" + io_.mint(fullPath(p), false)}};
    }

    IoChannel& io_;
};

}  // namespace

std::unique_ptr<IAdapter> makeFsAdapter(IoChannel& io) { return std::make_unique<FsAdapter>(io); }
