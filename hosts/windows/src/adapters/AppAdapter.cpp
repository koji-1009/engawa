// app namespace (spec/commands/app.md) — lifecycle + identity. engineInfo.contractVersion MUST equal
// engawa.contractVersion (§1.1); both come from kContractVersion. quit returns before the process
// ends. __exit is the autotest hook the make-notes app uses to end a launch with a code.
#include <windows.h>

#include <functional>

#include "Contract.hpp"
#include "adapters/Adapters.hpp"

namespace {

class AppAdapter : public IAdapter {
public:
    AppAdapter(std::string appVersion, std::function<std::string()> engineVersion,
               const HostOptions& opts, std::function<void()> onQuit)
        : appVersion_(appVersion.empty() ? "0.0.0" : std::move(appVersion)),
          engineVersion_(std::move(engineVersion)),
          autotestHooks_(opts.autotest || opts.conformance),
          onQuit_(std::move(onQuit)) {}

    std::string ns() const override { return "app"; }

    Json handle(const std::string& command, const Json& args) override {
        if (command == "version") return appVersion_;
        if (command == "engineInfo")
            return Json{{"engine", "WebView2"},
                        {"engineVersion", engineVersion_()},
                        {"hostVersion", kHostVersion},
                        {"contractVersion", kContractVersion}};
        if (command == "quit") {
            // Return first, then unwind the loop — app.quit "returns before the process exits".
            onQuit_();
            return Json(nullptr);
        }
        if (command == "__exit" && autotestHooks_) {
            int code = 0;
            double d;
            if (ja::tryGetDouble(args, "code", d)) code = static_cast<int>(d);
            ExitProcess(static_cast<UINT>(code));
        }
        throw EngawaError::nosys("app." + command);
    }

private:
    std::string appVersion_;
    std::function<std::string()> engineVersion_;
    bool autotestHooks_;
    std::function<void()> onQuit_;
};

}  // namespace

std::unique_ptr<IAdapter> makeAppAdapter(std::string appVersion, std::function<std::string()> engineVersion,
                                         const HostOptions& opts, std::function<void()> onQuit) {
    return std::make_unique<AppAdapter>(std::move(appVersion), std::move(engineVersion), opts, std::move(onQuit));
}
