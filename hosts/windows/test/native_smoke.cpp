// Native-path smoke test for the parts of dialog/notification that can run non-interactively.
//
// Covers, against the REAL code the host ships (src/NativeToast.cpp) and the real COM APIs:
//   • notification.show  — native::showToast fires an actual toast (asserts no throw).
//   • dialog.open/save   — the IFileOpenDialog COM object instantiates + configures.
//
// NOT covered (inherently interactive, and why the conformance suite uses a substitute instead): a
// user actually picking a file so IFileDialog::Show returns a path. Run via `make host-windows-smoke`.
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <cstdio>
#include <exception>

#include "NativeToast.hpp"

using Microsoft::WRL::ComPtr;

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int rc = 0;

    // notification.show — the real toast path (WinRT XML build, AUMID registration, notifier Show).
    try {
        native::showToast("Engawa", "native smoke test");
        std::printf("toast:  OK (fired; check Action Center)\n");
    } catch (const std::exception& e) {
        std::printf("toast:  FAIL %s\n", e.what());
        rc = 1;
    }

    // dialog.open/save — the COM object the adapter builds. Show() needs a user click, so the smoke
    // stops at create + configure (the part that breaks on a CLSID/COM/SDK regression).
    ComPtr<IFileOpenDialog> dlg;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (SUCCEEDED(hr) && dlg) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_FORCEFILESYSTEM);
        dlg->SetTitle(L"Engawa smoke");
        COMDLG_FILTERSPEC spec{L"Text", L"*.txt;*.md"};
        dlg->SetFileTypes(1, &spec);
        std::printf("dialog: OK (IFileOpenDialog created + configured)\n");
    } else {
        std::printf("dialog: FAIL CoCreateInstance hr=0x%08lx\n", static_cast<unsigned long>(hr));
        rc = 1;
    }

    std::printf(rc == 0 ? "SMOKE OK\n" : "SMOKE FAIL\n");
    return rc;
}
