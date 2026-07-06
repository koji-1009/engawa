// Engawa Linux reference host — C++ + GTK3 + WebKitGTK (hosts/linux/README.md).
//
// Entry point: resolve configuration from the environment, compose the adapters into the dispatcher,
// and boot a WebKitWebView on a GtkWindow. Everything protocol-shaped is in shell.js — this only
// assembles the pieces (contract §1). The binary is native and self-contained: the end user needs the
// system WebKitGTK + GTK runtime (docs/design.md).
#include <gtk/gtk.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "Bridge.hpp"
#include "Utf.hpp"
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
    auto o = Files::readJsonObject(U8(P(bundleRoot) / "engawa.json"));
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
    d.registerAdapter(makeClipboardAdapter(opts), &bridge);
    d.registerAdapter(makeShellOpenAdapter(opts), &bridge);
    d.registerAdapter(makeNotificationAdapter(opts), &bridge);
    d.registerAdapter(makeProcessAdapter(&bridge, opts), &bridge);
    d.registerAdapter(makeAppAdapter(appVersion, [&bridge] { return bridge.engineVersion(); }, opts,
                                     [&window] { window.post([&window] { window.closeWindow(); }); }),
                      &bridge);

    // `update` is contract-coupled (§7.1/§8): it versions with the contract and is composed into
    // EVERY host, not an app's choice (docs/design.md "Composition").
    d.registerAdapter(makeUpdateAdapter(updateHost, opts), &bridge);

    // The app's declared adapters (§3 static composition). registerAppAdapters is provided by a Compose
    // translation unit: the default in-tree build registers the reference `sqlite`; a per-app
    // `engawa dev/build` swaps in a generated TU that registers exactly what the app declared.
    registerAppAdapters(d, &bridge);
}

}  // namespace

int main(int argc, char** argv) {
    // A sidecar that closes its stdin or exits makes the next process.stdinWrite hit a broken pipe;
    // the default SIGPIPE disposition would kill the whole host. Ignore it so the write surfaces EIO.
    ::signal(SIGPIPE, SIG_IGN);

    // Headless (conformance / autotest gate) runs offscreen with no GPU (e.g. under WSLg): fall back to
    // software rendering so WebKit's web process reliably loads + runs JS. A visible `engawa dev` keeps
    // GPU acceleration. Set before gtk_init; never override a value the environment already chose.
    if (EnvOpt("ENGAWA_CONFORMANCE") == "1" || EnvOpt("ENGAWA_AUTOTEST") == "1") {
        setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 0);
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    }

    gtk_init(&argc, &argv);

    HostOptions opts = HostOptions::fromEnvironment();

    std::string shellJs;
    if (!Files::readAllBytes(opts.shellJsPath, shellJs)) {
        fprintf(stderr, "engawa host: cannot read shell.js at %s\n", opts.shellJsPath.c_str());
        return 1;
    }
    std::string appVersion = manifestString(opts.bundleRoot, "version").value_or("0.0.0");

    IoChannel io;
    bool hidden = opts.conformance || opts.autotest;  // offscreen for the suite / autotest gate
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
            bridge.showErrorScreen("This app needs a newer WebKitGTK runtime.\nDetected " + d + ", requires " + r + ".");
        };
    }

    // Kick off init once the loop is pumping — its callbacks resume on the main thread.
    window.post([&bridge] { bridge.startWebView(); });
    window.runMessageLoop();  // blocks until gtk_main_quit (app.quit / window close)
    return 0;
}
