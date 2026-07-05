# hosts/windows — C++ + WebView2 reference host

Clean-room from `spec/` + `shell-js/` + `conformance/` + adapter `spec.md`s only — the `hosts/macos/`
source is off-limits (CLAUDE.md clean-room rule). If blocked, the block is a spec hole: fix `spec/`,
then proceed. Definition of done: the conformance suite + `make notes` on Windows.

## What it is

A single native `EngawaHost.exe` (C++, WebView2) that calls the OS WebView directly. It implements the
two protocol primitives — **receive a string** (`WebMessageReceived`) and **evaluate a string**
(`ExecuteScript` of `__shell._deliver`) — and everything the contract layers on top:

- Boot handshake + injection (§1, §6): `__shell` + shell.js at document start via WebView2's
  user-script path (`AddScriptToExecuteOnDocumentCreated`, CSP-exempt §7.3), **top-level `app://`
  only** — a dead `__shell` on non-app documents, nothing in subframes. See `src/Bootstrap.cpp`.
- Wire protocol + flow control (§2, §2.1): one `_deliver` eval per UI tick, `window.resize`
  coalesced. See `src/Bridge.cpp`.
- `app://` scheme (§5, §5a; `spec/assets.md`): asset serving with the default CSP, MIME, 404/403,
  and the `app://io` binary channel (single-use tokens, CORS). See `src/SchemeHandler.cpp`.
- Built-in namespaces (§4) as in-tree adapters (`src/adapters/`); no privileged dispatch path (§3).
- Engine floor (§9), renderer-crash recovery (§10), storage-wipe boot (§10).
- Host obligations for `update` (§7.1 ed25519 trust, §8 atomic A/B slot swap): `src/UpdateHost.cpp`.

The window is a raw Win32 window (`src/Window.cpp`) hosting a `CoreWebView2Controller`; the control
channel (`src/ConformanceChannel.cpp`) is a background stdin reader that marshals onto the message
loop. The `sqlite` and `update` adapters' Windows sources live under `adapters/*/hosts/windows/` (§3
layout) and compile into this one exe — Engawa composes statically at app build time (§3, no dynamic
loading).

The shipped artifact is a self-contained native binary: the CRT and WebView2 loader are linked
statically, so nothing rides beside the exe and the end user installs no runtime — the only machine
dependency is the WebView2 Evergreen system component (docs/design.md).

## Build & verify

Needs the Visual Studio C++ workload (MSVC + Windows SDK; the CMake/Ninja that ship inside VS are
used automatically) and the WebView2 Evergreen runtime (present on Windows 11; installable on Windows
10 1809+). The WebView2 SDK and the vendored libraries under `third_party/` are restored/compiled at
build time; see `CMakeLists.txt` and `third_party/THIRD_PARTY.md`.

```
make host-windows     # hosts/windows/build.ps1 → CMake + Ninja → build/EngawaHost.exe
make conformance      # runs the suite against this host + the Node mock host (§11)
make notes            # the acceptance gate (examples/notes/gate/gate.ps1)
```

`make conformance` / `make notes` resolve to this host automatically on Windows (see the Makefile and
`conformance/hosts/windows/driver.js` — the runner picks the current platform's native host).
