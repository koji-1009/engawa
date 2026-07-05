// dialog namespace (spec/commands/dialog.md). open/save present the native Common Item Dialog
// (IFileOpenDialog / IFileSaveDialog) owned by the app window; message uses MessageBoxW. In
// conformance mode dialogs are modal + user-driven, so the host returns a preprogrammed response
// (dialog.__setResponse) instead of presenting UI (§ testability hook); argument validation still applies.
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <optional>
#include <vector>

#include "Utf.hpp"
#include "adapters/Adapters.hpp"

using Microsoft::WRL::ComPtr;

namespace {

std::string shellItemPath(IShellItem* item) {
    LPWSTR p = nullptr;
    std::string out;
    if (item && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) out = ToUtf8(p);
    if (p) CoTaskMemFree(p);
    return out;
}

class DialogAdapter : public IAdapter {
public:
    DialogAdapter(Window& window, const HostOptions& opts)
        : hwnd_(window.hwnd()), conformance_(opts.conformance) {}

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

    Json openDialog(const Json& args) {
        ComPtr<IFileOpenDialog> dlg;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
            return canceledOpen();

        DWORD opts = 0;
        dlg->GetOptions(&opts);
        opts |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST;
        if (ja::optBool(args, "directory")) opts |= FOS_PICKFOLDERS;
        if (ja::optBool(args, "multiple")) opts |= FOS_ALLOWMULTISELECT;
        dlg->SetOptions(opts);
        if (auto title = ja::optString(args, "title")) dlg->SetTitle(ToWide(*title).c_str());
        applyFilters(dlg.Get(), args);

        HRESULT hr = dlg->Show(hwnd_);
        if (FAILED(hr)) return canceledOpen();  // includes user cancel (ERROR_CANCELLED)

        Json paths = Json::array();
        ComPtr<IShellItemArray> items;
        if (SUCCEEDED(dlg->GetResults(&items)) && items) {
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD i = 0; i < count; i++) {
                ComPtr<IShellItem> item;
                if (SUCCEEDED(items->GetItemAt(i, &item))) {
                    std::string p = shellItemPath(item.Get());
                    if (!p.empty()) paths.push_back(p);
                }
            }
        }
        return Json{{"canceled", paths.empty()}, {"paths", paths}};
    }

    Json saveDialog(const Json& args) {
        ComPtr<IFileSaveDialog> dlg;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
            return canceledSave();

        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
        if (auto title = ja::optString(args, "title")) dlg->SetTitle(ToWide(*title).c_str());
        if (auto name = ja::optString(args, "defaultName")) dlg->SetFileName(ToWide(*name).c_str());

        HRESULT hr = dlg->Show(hwnd_);
        if (FAILED(hr)) return canceledSave();

        ComPtr<IShellItem> item;
        if (FAILED(dlg->GetResult(&item)) || !item) return canceledSave();
        std::string p = shellItemPath(item.Get());
        if (p.empty()) return canceledSave();
        return Json{{"canceled", false}, {"path", p}};
    }

    // filters (open): [{ name, extensions: [".txt", ...] }] → COMDLG_FILTERSPEC "*.txt;*.md".
    static void applyFilters(IFileOpenDialog* dlg, const Json& args) {
        const Json* f = ja::field(args, "filters");
        if (!f || !f->is_array() || f->empty()) return;
        std::vector<std::wstring> names, specs;  // own the strings for the .c_str() below
        for (const auto& entry : *f) {
            if (!entry.is_object()) continue;
            std::string name = entry.value("name", std::string());
            std::string spec;
            if (entry.contains("extensions") && entry["extensions"].is_array())
                for (const auto& e : entry["extensions"])
                    if (e.is_string()) spec += (spec.empty() ? "" : ";") + std::string("*") + e.get<std::string>();
            if (spec.empty()) continue;
            names.push_back(ToWide(name));
            specs.push_back(ToWide(spec));
        }
        if (names.empty()) return;
        std::vector<COMDLG_FILTERSPEC> fs;
        for (size_t i = 0; i < names.size(); i++) fs.push_back({names[i].c_str(), specs[i].c_str()});
        dlg->SetFileTypes(static_cast<UINT>(fs.size()), fs.data());
    }

    Json message(const Json& args) {
        std::string msg = ja::reqStringAllowEmpty(args, "message");  // present-but-empty legal; absent → EINVAL
        if (conformance_) return take(Json{{"button", 0}});
        std::string title = ja::optString(args, "title").value_or("");
        MessageBoxW(hwnd_, ToWide(msg).c_str(), ToWide(title).c_str(), MB_OK);
        return Json{{"button", 0}};  // only the default OK button (index 0) in this path
    }

    HWND hwnd_;
    bool conformance_;
    std::optional<Json> next_;
};

}  // namespace

std::unique_ptr<IAdapter> makeDialogAdapter(Window& window, const HostOptions& opts) {
    return std::make_unique<DialogAdapter>(window, opts);
}
