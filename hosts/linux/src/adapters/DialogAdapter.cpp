// dialog namespace (spec/commands/dialog.md). open/save present the native GTK file chooser
// (GtkFileChooserDialog) parented to the app window; message uses a GtkDialog with the requested
// button labels. In conformance mode dialogs are modal + user-driven, so the host returns a
// preprogrammed response (dialog.__setResponse) instead of presenting UI (§ testability hook);
// argument validation still applies.
#include <gtk/gtk.h>

#include <optional>
#include <vector>

#include "adapters/Adapters.hpp"

namespace {

class DialogAdapter : public IAdapter {
public:
    DialogAdapter(Window& window, const HostOptions& opts)
        : parent_(window.gtkWindow()), conformance_(opts.conformance) {}

    std::string ns() const override { return "dialog"; }

    Json handle(const std::string& command, const Json& args) override {
        if (command == "open") return conformance_ ? take(canceledOpen()) : openDialog(args);
        if (command == "save") return conformance_ ? take(canceledSave()) : saveDialog(args);
        if (command == "message") return message(args);
        if (command == "__setResponse" && conformance_) {
            next_ = args;
            return Json(nullptr);
        }
        throw EngawaError::nosys("dialog." + command);
    }

private:
    static Json canceledOpen() { return Json{{"canceled", true}, {"paths", Json::array()}}; }
    static Json canceledSave() { return Json{{"canceled", true}, {"path", nullptr}}; }

    Json take(Json fallback) {
        Json r = next_ ? *next_ : fallback;
        next_.reset();
        return r;
    }

    GtkWindow* parentWindow() { return parent_ ? GTK_WINDOW(parent_) : nullptr; }

    Json openDialog(const Json& args) {
        bool directory = ja::optBool(args, "directory");
        bool multiple = ja::optBool(args, "multiple");
        std::string title = ja::optString(args, "title").value_or(directory ? "Select Folder" : "Open");

        GtkFileChooserAction action = directory ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
                                                : GTK_FILE_CHOOSER_ACTION_OPEN;
        GtkWidget* d = gtk_file_chooser_dialog_new(title.c_str(), parentWindow(), action, "_Cancel",
                                                   GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        GtkFileChooser* chooser = GTK_FILE_CHOOSER(d);
        gtk_file_chooser_set_local_only(chooser, TRUE);  // real local paths only (FORCEFILESYSTEM parity)
        gtk_file_chooser_set_select_multiple(chooser, multiple);
        applyFilters(chooser, args);

        Json paths = Json::array();
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
            GSList* files = gtk_file_chooser_get_filenames(chooser);
            for (GSList* it = files; it; it = it->next) {
                char* p = static_cast<char*>(it->data);
                if (p) paths.push_back(std::string(p));
                g_free(p);
            }
            g_slist_free(files);
        }
        gtk_widget_destroy(d);
        return Json{{"canceled", paths.empty()}, {"paths", paths}};
    }

    Json saveDialog(const Json& args) {
        std::string title = ja::optString(args, "title").value_or("Save As");
        GtkWidget* d = gtk_file_chooser_dialog_new(title.c_str(), parentWindow(),
                                                   GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel",
                                                   GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        GtkFileChooser* chooser = GTK_FILE_CHOOSER(d);
        gtk_file_chooser_set_local_only(chooser, TRUE);  // real local paths only (FORCEFILESYSTEM parity)
        gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
        if (auto name = ja::optString(args, "defaultName")) gtk_file_chooser_set_current_name(chooser, name->c_str());

        Json result = canceledSave();
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
            char* p = gtk_file_chooser_get_filename(chooser);
            if (p) { result = Json{{"canceled", false}, {"path", std::string(p)}}; g_free(p); }
        }
        gtk_widget_destroy(d);
        return result;
    }

    // filters (open): [{ name, extensions: [".txt", ...] }] → a GtkFileFilter with "*.txt" patterns.
    static void applyFilters(GtkFileChooser* chooser, const Json& args) {
        const Json* f = ja::field(args, "filters");
        if (!f || !f->is_array()) return;
        for (const auto& entry : *f) {
            if (!entry.is_object()) continue;
            GtkFileFilter* filter = gtk_file_filter_new();
            gtk_file_filter_set_name(filter, entry.value("name", std::string("Files")).c_str());
            bool any = false;
            if (entry.contains("extensions") && entry["extensions"].is_array())
                for (const auto& e : entry["extensions"])
                    if (e.is_string()) {
                        gtk_file_filter_add_pattern(filter, ("*" + e.get<std::string>()).c_str());
                        any = true;
                    }
            if (any) gtk_file_chooser_add_filter(chooser, filter);
            else g_object_ref_sink(filter), g_object_unref(filter);  // unused floating filter
        }
    }

    Json message(const Json& args) {
        std::string msg = ja::reqStringAllowEmpty(args, "message");  // present-but-empty legal; absent → EINVAL
        if (conformance_) return take(Json{{"button", 0}});

        std::string title = ja::optString(args, "title").value_or("Engawa");

        // buttons (spec/commands/dialog.md): the label array, default ["OK"]; returns the clicked
        // 0-based index — which we use directly as the GTK response id.
        std::vector<std::string> labels;
        const Json* b = ja::field(args, "buttons");
        if (b && b->is_array())
            for (const auto& e : *b)
                if (e.is_string()) labels.push_back(e.get<std::string>());
        if (labels.empty()) labels.push_back("OK");

        GtkWidget* d = gtk_message_dialog_new(parentWindow(), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                                              GTK_BUTTONS_NONE, "%s", msg.c_str());
        gtk_window_set_title(GTK_WINDOW(d), title.c_str());
        for (size_t i = 0; i < labels.size(); i++)
            gtk_dialog_add_button(GTK_DIALOG(d), labels[i].c_str(), static_cast<gint>(i));

        gint resp = gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        int button = (resp >= 0 && resp < static_cast<int>(labels.size())) ? resp : 0;
        return Json{{"button", button}};
    }

    GtkWidget* parent_;
    bool conformance_;
    std::optional<Json> next_;
};

}  // namespace

std::unique_ptr<IAdapter> makeDialogAdapter(Window& window, const HostOptions& opts) {
    return std::make_unique<DialogAdapter>(window, opts);
}
