// shellOpen namespace (spec/commands/shellOpen.md) — the sanctioned hand-off out of the app. In
// conformance mode the commands have no readable result and OS side effects, so the host records each
// request (exposed via shellOpen.__recorded) instead of launching a browser.
#include <windows.h>
#include <shellapi.h>

#include <algorithm>

#include "PathUtil.hpp"
#include "Utf.hpp"
#include "adapters/Adapters.hpp"

namespace {

// Only user-web schemes: keeps "open a link" from becoming a local-file / script surface (§7).
bool allowedScheme(const std::string& url) {
    auto colon = url.find(':');
    if (colon == std::string::npos || colon == 0) return false;
    std::string s = url.substr(0, colon);
    for (char c : s)
        if (!(std::isalnum((unsigned char)c) || c == '+' || c == '.' || c == '-')) return false;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (!(s[0] >= 'a' && s[0] <= 'z')) return false;
    return s == "http" || s == "https" || s == "mailto" || s == "tel";
}

class ShellOpenAdapter : public IAdapter {
public:
    explicit ShellOpenAdapter(const HostOptions& opts) : conformance_(opts.conformance) {}

    std::string ns() const override { return "shellOpen"; }

    Json handle(const std::string& command, const Json& args) override {
        if (command == "openExternal") return openExternal(ja::reqString(args, "url"));
        if (command == "revealInFolder") return reveal(ja::reqString(args, "path"));
        if (command == "__recorded" && conformance_) return recorded_;
        throw EngawaError::nosys("shellOpen." + command);
    }

private:
    Json openExternal(const std::string& url) {
        if (!allowedScheme(url))
            throw EngawaError::invalid("unsupported url scheme (http, https, mailto, tel only)");
        if (conformance_) {
            recorded_.push_back(Json{{"action", "openExternal"}, {"url", url}});
            return Json(nullptr);
        }
        ShellExecuteW(nullptr, L"open", ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return Json(nullptr);
    }

    Json reveal(const std::string& path) {
        if (!Files::exists(path)) throw EngawaError::noent("no such path: " + path);
        if (conformance_) {
            recorded_.push_back(Json{{"action", "revealInFolder"}, {"path", path}});
            return Json(nullptr);
        }
        std::wstring arg = L"/select,\"" + ToWide(path) + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        return Json(nullptr);
    }

    bool conformance_;
    Json recorded_ = Json::array();
};

}  // namespace

std::unique_ptr<IAdapter> makeShellOpenAdapter(const HostOptions& opts) {
    return std::make_unique<ShellOpenAdapter>(opts);
}
