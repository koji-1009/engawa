// path namespace (spec/commands/path.md). The three per-app directories are created on demand so a
// returned path is always usable; paths are absolute and stable within a run. Under conformance they
// hang off ENGAWA_DATA_ROOT (isolated + cleaned up by the runner); otherwise off the XDG data dir
// (HostOptions resolves the default). home is $HOME; temp is $TMPDIR (else /tmp).
#include "PathUtil.hpp"
#include "Utf.hpp"
#include "adapters/Adapters.hpp"

namespace {

std::string ensure(const std::string& p) {
    std::error_code ec;
    fsys::create_directories(P(p), ec);
    if (ec) throw EngawaError::io(ec.message());
    return U8(fsys::absolute(P(p), ec));
}

class PathAdapter : public IAdapter {
public:
    explicit PathAdapter(const HostOptions& opts) {
        appData_ = U8(P(opts.dataRoot) / "data");
        appConfig_ = U8(P(opts.dataRoot) / "config");
        appCache_ = U8(P(opts.dataRoot) / "cache");
    }

    std::string ns() const override { return "path"; }

    Json handle(const std::string& command, const Json&) override {
        if (command == "appData") return ensure(appData_);
        if (command == "appConfig") return ensure(appConfig_);
        if (command == "appCache") return ensure(appCache_);
        if (command == "home") return EnvOpt("HOME").value_or("");
        if (command == "temp") {
            std::string t = EnvOpt("TMPDIR").value_or("/tmp");
            while (t.size() > 1 && t.back() == '/') t.pop_back();
            return t;
        }
        throw EngawaError::nosys("path." + command);
    }

private:
    std::string appData_, appConfig_, appCache_;
};

}  // namespace

std::unique_ptr<IAdapter> makePathAdapter(const HostOptions& opts) {
    return std::make_unique<PathAdapter>(opts);
}
