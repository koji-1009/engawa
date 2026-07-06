# hosts/linux — Linux reference host (C++ + GTK3 + WebKitGTK)

The Linux reference host: a small native C++ program that hosts a `WebKitWebView` on a `GtkWindow` and
injects `__shell` + shell.js. Implemented **clean-room** from `spec/` + `shell-js/` + `conformance/` +
adapter `spec.md`s only (the `hosts/macos/` source is off-limits, CLAUDE.md clean-room rule). It shares
the platform-neutral core with the Windows host (same Dispatcher / Bridge protocol / adapter logic);
only the platform layer differs — GTK3 + WebKitGTK (webkit2gtk-4.1) instead of Win32 + WebView2, and
libsodium instead of Windows CNG + tweetnacl for §7.1 crypto.

## Layout

```
hosts/linux/
  src/                 host core + adapters (the platform layer: Window, Bridge, SchemeHandler, …)
  engawa-host.cmake    reusable composition: resolves gtk/webkit/soup/sodium, fetches json, defines
                       engawa_add_host(TARGET ADAPTERS <dirs> COMPOSE <file>)
  CMakeLists.txt       the in-tree reference host (composes the reference `sqlite` + default Compose.cpp)
  build.sh             cmake + ninja build (make host-linux)
```

## Build

Needs `g++`, `cmake`, `ninja`, and the `-dev` packages for `gtk+-3.0`, `webkit2gtk-4.1`,
`libsoup-3.0`, `libsodium`:

```
sudo apt install build-essential cmake ninja-build pkg-config \
     libgtk-3-dev libwebkit2gtk-4.1-dev libsoup-3.0-dev libsodium-dev
make host-linux          # → hosts/linux/build/EngawaHost
```

`nlohmann/json` (the wire codec) and the SQLite amalgamation are fetched pinned + SHA-256-checked at
configure time into the git-ignored `build/_deps` (never committed). Crypto (§7.1 SHA-256 + ed25519)
is system libsodium.

## Definition of done

`make conformance` (Linux native host + Node mock) and `make notes` (examples/notes acceptance gate),
per CLAUDE.md. Both run offscreen with software rendering (the host sets `WEBKIT_DISABLE_COMPOSITING_MODE`
/ `LIBGL_ALWAYS_SOFTWARE` under `ENGAWA_CONFORMANCE`/`ENGAWA_AUTOTEST` so WebKit's web process loads and
runs without a GPU); a visible `engawa dev` keeps GPU acceleration.

**A display server is still required.** GtkOffscreenWindow renders offscreen *within* an X/Wayland
session, but `gtk_init` needs a live `$DISPLAY`/`$WAYLAND_DISPLAY` — it is not truly headless. WSLg
provides one; a bare CI runner must wrap the gates in `xvfb-run` (or start an Xvfb + export `DISPLAY`).

## Host obligations (not adapters)

Contract §7.1 (ed25519 verification against the compiled-in trust root before anything lands under
`app://`) and the atomic A/B asset-root swap (§8) are **host** duties, implemented in
`src/UpdateHost.cpp` + `src/Crypto.cpp` — a host is non-conformant without them regardless of the
`update` adapter's presence.
