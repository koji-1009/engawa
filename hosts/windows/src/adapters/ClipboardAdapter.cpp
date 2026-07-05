// clipboard namespace (spec/commands/clipboard.md) — the system clipboard over raw Win32. Text is
// UTF-16 (CF_UNICODETEXT) and round-trips unchanged; readText returns "" for an empty/non-text
// clipboard. The clipboard is a single shared, lockable resource, so Open is retried briefly. Runs
// on the STA UI thread (dispatch marshals there).
#include <windows.h>

#include <functional>
#include <thread>

#include "Utf.hpp"
#include "adapters/Adapters.hpp"

namespace {

void withClipboard(const std::function<void()>& body) {
    for (int i = 0;; i++) {
        if (OpenClipboard(nullptr)) {
            try { body(); } catch (...) { CloseClipboard(); throw; }
            CloseClipboard();
            return;
        }
        if (i >= 30) throw EngawaError::io("clipboard is locked by another process");
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}

class ClipboardAdapter : public IAdapter {
public:
    std::string ns() const override { return "clipboard"; }

    Json handle(const std::string& command, const Json& args) override {
        if (command == "writeText") { writeText(ja::reqStringAllowEmpty(args, "text")); return Json(nullptr); }
        if (command == "readText") return readText();
        throw EngawaError::nosys("clipboard." + command);
    }

private:
    static void writeText(const std::string& textUtf8) {
        std::wstring text = ToWide(textUtf8);
        withClipboard([&] {
            EmptyClipboard();
            if (text.empty()) return;  // clearing is the empty-string equivalent
            SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (!hMem) throw EngawaError::io("GlobalAlloc failed");
            void* ptr = GlobalLock(hMem);
            if (!ptr) { GlobalFree(hMem); throw EngawaError::io("GlobalLock failed"); }
            memcpy(ptr, text.c_str(), bytes);  // includes the NUL terminator
            GlobalUnlock(hMem);
            if (!SetClipboardData(CF_UNICODETEXT, hMem)) { GlobalFree(hMem); throw EngawaError::io("SetClipboardData failed"); }
            // On success the system owns hMem — do not free it.
        });
    }

    static Json readText() {
        std::string result;
        withClipboard([&] {
            if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return;
            HANDLE h = GetClipboardData(CF_UNICODETEXT);
            if (!h) return;
            const wchar_t* ptr = static_cast<const wchar_t*>(GlobalLock(h));
            if (!ptr) return;
            result = ToUtf8(ptr);
            GlobalUnlock(h);
        });
        return result;
    }
};

}  // namespace

std::unique_ptr<IAdapter> makeClipboardAdapter() { return std::make_unique<ClipboardAdapter>(); }
