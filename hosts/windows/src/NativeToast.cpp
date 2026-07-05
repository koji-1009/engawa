#include "NativeToast.hpp"

#include <windows.h>
#include <shobjidl.h>  // SetCurrentProcessExplicitAppUserModelID, IShellLinkW
#include <shlobj.h>    // SHGetKnownFolderPath, CLSID_ShellLink, FOLDERID_Programs
#include <propvarutil.h>
#include <propsys.h>   // IPropertyStore
#include <wrl/client.h>

#include <stdexcept>
#include <string>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>

#include "Utf.hpp"

namespace {

using winrt::Windows::Data::Xml::Dom::XmlDocument;
using winrt::Windows::UI::Notifications::ToastNotification;
using winrt::Windows::UI::Notifications::ToastNotificationManager;

std::wstring escapeXml(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            case L'\'': out += L"&apos;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

// Toasts from an unpackaged Win32 app need a process AppUserModelID that is registered (with a
// DisplayName) so Windows has a name to attribute the notification to. Register once in HKCU.
const wchar_t* kAumid = L"dev.engawa.host";

// Windows only surfaces toasts from an unpackaged Win32 app if a Start-Menu shortcut carries the
// process's AppUserModelID (registry registration alone often creates-but-does-not-show the toast).
// Create one once, pointing at this exe, with the AUMID property.
void ensureStartMenuShortcut() {
    PWSTR programs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs)) || !programs) return;
    std::wstring lnk = std::wstring(programs) + L"\\Engawa.lnk";
    CoTaskMemFree(programs);
    if (GetFileAttributesW(lnk.c_str()) != INVALID_FILE_ATTRIBUTES) return;  // already created

    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return;

    Microsoft::WRL::ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return;
    link->SetPath(exe);
    Microsoft::WRL::ComPtr<IPropertyStore> store;
    if (SUCCEEDED(link.As(&store))) {
        // PKEY_AppUserModel_ID, defined locally to avoid the INITGUID/propsys-lib dance.
        static const PROPERTYKEY kAumidKey = {
            {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
        PROPVARIANT pv;
        if (SUCCEEDED(InitPropVariantFromString(kAumid, &pv))) {
            store->SetValue(kAumidKey, pv);
            store->Commit();
            PropVariantClear(&pv);
        }
    }
    Microsoft::WRL::ComPtr<IPersistFile> file;
    if (SUCCEEDED(link.As(&file))) file->Save(lnk.c_str(), TRUE);
}

void ensureAumidRegistered() {
    static bool done = false;
    if (done) return;
    done = true;
    SetCurrentProcessExplicitAppUserModelID(kAumid);
    HKEY key = nullptr;
    std::wstring sub = std::wstring(L"Software\\Classes\\AppUserModelId\\") + kAumid;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) ==
        ERROR_SUCCESS) {
        const wchar_t* name = L"Engawa";
        RegSetValueExW(key, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE*>(name),
                       static_cast<DWORD>((wcslen(name) + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
    ensureStartMenuShortcut();
}

}  // namespace

namespace native {

void showToast(const std::string& title, const std::string& body) {
    try {
        ensureAumidRegistered();
        std::wstring xml = L"<toast><visual><binding template='ToastGeneric'><text>" +
                           escapeXml(ToWide(title)) + L"</text><text>" + escapeXml(ToWide(body)) +
                           L"</text></binding></visual></toast>";
        XmlDocument doc;
        doc.LoadXml(winrt::hstring(xml));
        ToastNotificationManager::CreateToastNotifier(winrt::hstring(kAumid)).Show(ToastNotification(doc));
    } catch (const winrt::hresult_error& e) {
        throw std::runtime_error("notification failed: " + ToUtf8(e.message().c_str()));
    }
}

}  // namespace native
