// notification namespace (spec/commands/notification.md). On the real path this posts a system toast
// via native::showToast (org.freedesktop.Notifications over D-Bus); under conformance the command has an OS side
// effect and no readable result, so the host records each request and exposes it via __recorded.
#include <exception>

#include "NativeToast.hpp"
#include "adapters/Adapters.hpp"

namespace {

class NotificationAdapter : public IAdapter {
public:
    explicit NotificationAdapter(const HostOptions& opts) : conformance_(opts.conformance) {}

    std::string ns() const override { return "notification"; }

    Json handle(const std::string& command, const Json& args) override {
        if (command == "show") {
            std::string title = ja::reqString(args, "title");
            std::string body = ja::optString(args, "body").value_or("");
            if (conformance_) {
                recorded_.push_back(Json{{"title", title}, {"body", body}});
            } else {
                // Best effort: a toast can fail (no Action Center, group policy); surface EIO rather
                // than crash so the app can fall back.
                try {
                    native::showToast(title, body);
                } catch (const std::exception& e) {
                    throw EngawaError::io(e.what());
                }
            }
            return Json(nullptr);
        }
        if (command == "__recorded" && conformance_) return recorded_;
        throw EngawaError::nosys("notification." + command);
    }

private:
    bool conformance_;
    Json recorded_ = Json::array();
};

}  // namespace

std::unique_ptr<IAdapter> makeNotificationAdapter(const HostOptions& opts) {
    return std::make_unique<NotificationAdapter>(opts);
}
