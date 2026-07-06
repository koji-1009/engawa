// clipboard namespace (spec/commands/clipboard.md) — the system clipboard via GTK. Text is UTF-8 and
// round-trips unchanged. Runs on the GTK main thread (dispatch marshals there), which is where
// GtkClipboard must be used.
//
// GtkClipboard's set_text / wait_for_text run a nested main loop that blocks until a selection
// broker/manager replies — which never happens under a broker-less display (the offscreen
// conformance/autotest run, and `engawa dev` under WSLg), wedging the single-threaded dispatch with no
// timeout. So readText NEVER calls the blocking wait_for_text: it returns an in-process mirror of what
// this app last wrote (the write->read contract the suite and apps rely on). writeText still pushes to
// the real system clipboard on a normal desktop session — a visible window can own the selection — but
// under headless (offscreen) it stays in-process only, since even set_text blocks there. This keeps the
// clipboard non-blocking on every platform rather than trusting a test-mode flag to gate the hang.
#include <gtk/gtk.h>

#include <string>

#include "adapters/Adapters.hpp"

namespace {

GtkClipboard* clip() { return gtk_clipboard_get(GDK_SELECTION_CLIPBOARD); }

class ClipboardAdapter : public IAdapter {
public:
    explicit ClipboardAdapter(const HostOptions& opts) : headless_(opts.conformance || opts.autotest) {}

    std::string ns() const override { return "clipboard"; }

    Json handle(const std::string& command, const Json& args) override {
        if (command == "writeText") { writeText(ja::reqStringAllowEmpty(args, "text")); return Json(nullptr); }
        if (command == "readText") return readText();
        throw EngawaError::nosys("clipboard." + command);
    }

private:
    void writeText(const std::string& text) {
        mirror_ = text;
        // Populate the real system clipboard for other apps — but only with a visible window to own the
        // selection; offscreen (headless) set_text blocks on a broker that never answers.
        if (!headless_) gtk_clipboard_set_text(clip(), text.c_str(), static_cast<gint>(text.size()));
    }

    Json readText() { return mirror_; }  // never the blocking wait_for_text (see header note)

    bool headless_;
    std::string mirror_;  // what this app last wrote; "" until first write
};

}  // namespace

std::unique_ptr<IAdapter> makeClipboardAdapter(const HostOptions& opts) {
    return std::make_unique<ClipboardAdapter>(opts);
}
