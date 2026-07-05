// Engawa Windows reference host — C++ + WebView2 (hosts/windows/README.md).
//
// Entry point: resolve configuration from the environment, compose the adapters into the dispatcher,
// and boot WebView2 on a raw Win32 window. Everything protocol-shaped is in shell.js — this only
// assembles the pieces (contract §1). The binary is native and self-contained: the end user needs no
// runtime, only the WebView2 Evergreen system component (docs/design.md).
#include <windows.h>
#include <commctrl.h>
#include <fcntl.h>
#include <io.h>

// Pull in Common Controls v6 so dialog.message can use TaskDialog for custom buttons (v5, the
// default without a manifest, has no TaskDialog).
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <cstdio>
#include <optional>
#include <string>

#include "Bridge.hpp"
#include "Compose.hpp"
#include "ConformanceChannel.hpp"
#include "Contract.hpp"
#include "Dispatcher.hpp"
#include "HostOptions.hpp"
#include "IoChannel.hpp"
#include "PathUtil.hpp"
#include "UpdateHost.hpp"
#include "Window.hpp"
#include "adapters/Adapters.hpp"

namespace {

std::optional<std::string> manifestString(const std::string& bundleRoot, const std::string& key) {
    auto o = Files::readJsonObject(U8(P(bundleRoot) / L"engawa.json"));
    if (!o || !o->contains(key) || !(*o)[key].is_string()) return std::nullopt;
    return (*o)[key].get<std::string>();
}

// §7.3: the default CSP plus any engawa.json relaxations, applied verbatim (no silent widening).
std::string buildCsp(const std::string& bundleRoot) {
    std::string csp = "default-src app:; script-src app:";
    auto relax = manifestString(bundleRoot, "csp");
    if (relax && !relax->empty()) csp += "; " + *relax;
    return csp;
}

void registerAdapters(Dispatcher& d, Bridge& bridge, Window& window, const HostOptions& opts,
                      IoChannel& io, const std::string& appVersion, UpdateHost& updateHost) {
    // The vertical-slice command (conformance echo/limits tests).
    d.registerAdapter(makeEchoAdapter(), &bridge);

    // Built-in namespaces (§4) — adapters that ship in-tree; no privileged dispatch path (§3).
    d.registerAdapter(makeWindowAdapter(window, &bridge, opts), &bridge);
    d.registerAdapter(makeDialogAdapter(window, opts), &bridge);
    d.registerAdapter(makeFsAdapter(io), &bridge);
    d.registerAdapter(makePathAdapter(opts), &bridge);
    d.registerAdapter(makeClipboardAdapter(), &bridge);
    d.registerAdapter(makeShellOpenAdapter(opts), &bridge);
    d.registerAdapter(makeNotificationAdapter(opts), &bridge);
    d.registerAdapter(makeProcessAdapter(&bridge, opts), &bridge);
    d.registerAdapter(makeAppAdapter(appVersion, [&bridge] { return bridge.engineVersion(); }, opts,
                                     [&window] { window.post([&window] { window.closeWindow(); }); }),
                      &bridge);

    // `update` is contract-coupled (§7.1/§8): it versions with the contract and is composed into
    // EVERY host, not an app's choice (docs/design.md "Composition").
    d.registerAdapter(makeUpdateAdapter(updateHost, opts), &bridge);

    // The app's declared adapters (§3 static composition). registerAppAdapters is provided by a
    // Compose translation unit: the default in-tree build registers the reference `sqlite`; a
    // per-app `engawa dev/build` swaps in a generated TU that registers exactly what the app declared.
    registerAppAdapters(d, &bridge);
}

}  // namespace

int main() {
    // Per-monitor DPI awareness (must be set before any window is created): otherwise Windows
    // bitmap-stretches the window on a high-DPI display and WebView2 renders blurry.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Common Controls v6 (for TaskDialog in dialog.message).
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    // The control channel is byte-exact newline-delimited JSON; keep the standard streams from
    // translating CRLF (the driver writes and expects bare '\n').
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    // WebView2 hosts on an STA UI thread that pumps messages.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HostOptions opts = HostOptions::fromEnvironment();

    std::string shellJs;
    if (!Files::readAllBytes(opts.shellJsPath, shellJs)) {
        fprintf(stderr, "engawa host: cannot read shell.js at %s\n", opts.shellJsPath.c_str());
        return 1;
    }
    std::string appVersion = manifestString(opts.bundleRoot, "version").value_or("0.0.0");

    IoChannel io;
    bool hidden = opts.conformance || opts.autotest;  // off-screen for the suite / autotest gate
    Window window(hidden);

    // Trust root (§7.1): the compiled-in key wins — a distributable can't have it swapped. Only when
    // none is baked in (the reference/dev host) do we fall back to the environment (dev/conformance).
    std::string trustRoot = bakedTrustRoot();
    if (trustRoot.empty()) trustRoot = opts.trustRootB64;

    // Host obligation (§8): the app:// root is an A/B slot indirection, seeded from the initial tree.
    UpdateHost updateHost(opts.dataRoot, opts.appRoot, appVersion, trustRoot);
    Dispatcher dispatcher;
    std::string csp = buildCsp(opts.bundleRoot);
    Bridge bridge(window, opts, dispatcher, shellJs, io, [&updateHost] { return updateHost.liveRoot(); }, csp);

    registerAdapters(dispatcher, bridge, window, opts, io, appVersion, updateHost);

    ConformanceChannel channel;
    if (opts.conformance) {
        channel.wire(&bridge);
        bridge.onReady = [&channel] { channel.emitReady(); };
        bridge.onFloorRejected = [&channel](std::string d, std::string r) { channel.emitFloorRejected(d, r); };
        channel.start();
    } else {
        bridge.onFloorRejected = [&bridge](std::string d, std::string r) {
            bridge.showErrorScreen("This app needs a newer WebView2 runtime.\nDetected " + d + ", requires " + r + ".");
        };
    }

    // Kick off async init once the loop is pumping — its callbacks resume on the UI thread.
    window.post([&bridge] { bridge.startWebView(); });
    window.runMessageLoop();  // blocks until PostQuitMessage (app.quit / window close)
    return 0;
}
