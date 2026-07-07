// window namespace (spec/commands/window.md). Holds the logical window model and the §4.2 close
// gate; delegates actual window ops to Window. getSize returns the logical size the app last set,
// independent of DPI scaling, so setSize→getSize round-trips exactly (contract §4.2 testability).
#include <optional>
#include <unordered_set>

#include "adapters/Adapters.hpp"

namespace {

class WindowAdapter : public IAdapter {
public:
    WindowAdapter(Window& window, IEventEmitter* emitter, const HostOptions& opts)
        : window_(window), emitter_(emitter), conformance_(opts.conformance) {
        // A real user close attempt runs through the SAME gate as the conformance requestClose hook.
        window_.onUserCloseAttempt = [this]() -> bool {
            if (!interceptClose_) return false;  // default: the window just closes (§4.2)
            emitCloseRequested();
            return true;  // opted in: cancel the OS close, wait indefinitely
        };
        window_.onUserResized = [this](int w, int h) {
            width_ = w;
            height_ = h;
            emitter_->emit("window.resize", size());
        };
    }

    std::string ns() const override { return "window"; }

    Json handle(const std::string& command, const Json& args) override {
        if (command == "setTitle") {
            window_.applyTitle(ja::optString(args, "title").value_or(""));
            return Json(nullptr);
        }
        if (command == "getSize") return size();
        if (command == "setSize") return setSize(args);
        if (command == "setResizable") {
            window_.applyResizable(ja::optBool(args, "resizable"));
            return Json(nullptr);
        }
        if (command == "minimize") { window_.doMinimize(); return Json(nullptr); }
        if (command == "maximize") { window_.doMaximize(); return Json(nullptr); }
        if (command == "close") { window_.closeWindow(); return Json(nullptr); }  // never emits closeRequested (§4.2)
        if (command == "setCloseHandler") {
            interceptClose_ = ja::optBool(args, "enabled");
            return Json(nullptr);
        }
        if (command == "respondToClose") return respondToClose(args);
        if (command == "requestClose" && conformance_) return requestClose();
        if (command == "__resizeStorm" && conformance_) return resizeStorm(args);
        if (command == "__lastCloseAllowed" && conformance_)
            return lastCloseAllowed_ ? Json(*lastCloseAllowed_) : Json(nullptr);
        throw EngawaError::nosys("window." + command);
    }

private:
    Json size() { return Json{{"width", width_}, {"height", height_}}; }

    Json setSize(const Json& args) {
        double w = 0, h = 0;  // set by tryGetDouble on success; init quiets a false-positive C4701
        if (!ja::tryGetDouble(args, "width", w) || !ja::tryGetDouble(args, "height", h))
            throw EngawaError::invalid("width/height required");
        width_ = static_cast<int>(w);
        height_ = static_cast<int>(h);
        window_.applyClientSize(width_, height_);
        emitter_->emit("window.resize", size());
        return Json(nullptr);
    }

    Json respondToClose(const Json& args) {
        long long token = ja::reqInt(args, "token");
        if (closeTokens_.erase(token) == 0)
            throw EngawaError::invalid("unknown or consumed close token");  // §4.2 → EINVAL
        bool allow = ja::optBool(args, "allow");
        lastCloseAllowed_ = allow;
        // Under conformance the "user close" is the requestClose hook; actually destroying the window
        // would kill the suite's host, so record the decision but don't close.
        if (allow && !conformance_) window_.closeWindow();
        return Json(nullptr);
    }

    Json requestClose() {
        if (!interceptClose_) return Json{{"deferred", false}};
        emitCloseRequested();
        return Json{{"deferred", true}};
    }

    void emitCloseRequested() {
        long long token = ++tokenSeq_;
        closeTokens_.insert(token);
        emitter_->emit("window.closeRequested", Json{{"token", token}});
    }

    Json resizeStorm(const Json& args) {
        double c, f;
        int count = ja::tryGetDouble(args, "count", c) ? static_cast<int>(c) : 8;
        int from = ja::tryGetDouble(args, "from", f) ? static_cast<int>(f) : 300;
        for (int i = 0; i < count; i++)
            emitter_->emit("window.resize", Json{{"width", from + i}, {"height", from + i}});
        return Json{{"from", from}, {"count", count}, {"last", from + count - 1}};
    }

    Window& window_;
    IEventEmitter* emitter_;
    bool conformance_;
    int width_ = 1024, height_ = 720;
    bool interceptClose_ = false;
    std::unordered_set<long long> closeTokens_;
    long long tokenSeq_ = 0;
    std::optional<bool> lastCloseAllowed_;
};

}  // namespace

std::unique_ptr<IAdapter> makeWindowAdapter(Window& window, IEventEmitter* emitter, const HostOptions& opts) {
    return std::make_unique<WindowAdapter>(window, emitter, opts);
}
