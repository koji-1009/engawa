# Engawa Design

The contract (`spec/`) defines *what*; this document states *why it is built this way*.

## Thesis

A desktop app is a web app plus a fixed set of native services: windows, dialogs, filesystem, notifications, processes, durable data, self-managed updates. Engawa provides exactly that set, as a **specification** — one protocol contract, implemented independently on each platform in that platform's first-class native language, rendering through the OS WebView.

Each host compiles to a native binary that calls the OS WebView directly. The app the end user runs carries no runtime prerequisite — the only dependency on the user's machine is the OS WebView itself, a system component. Build-time toolchains and SDKs are the developer's cost; the shipped app is a plain native binary.

The engawa — the veranda between house and garden — is the contract layer between native and web. An Engawa app is **JS + adapters**.

## Structure

| Layer | What it is |
|-------|-----------|
| `spec/` | The product. Normative contract: handshake, wire protocol, command set, resilience guarantees. |
| `conformance/` | Executable form of the spec. A host is conformant when the suite passes — on the real host and on the Node mock host. |
| `shell-js/` | Shared JS runtime, identical bytes on every host. Owns promise correlation, invoke, events. |
| Hosts | One per platform, no shared build module. macOS: Swift + WKWebView. Windows: C++ + WebView2. Linux: C++ + GTK3 + WebKitGTK. Each is a small native program on its platform's most-paved path. The two C++ hosts are independent (their own copy of the platform-neutral core, near-identical by construction — clean-room from the same spec); they diverge in the platform layer (window, webview, crypto). |
| Adapters | The extension model and the app model. 1 adapter = 1 namespace = 1 repo, consumed as a hash-pinned source dependency, compiled into the host at app build time. |

Hosts implement two primitives — receive a string, evaluate a string — and a set of command handlers. Everything protocol-shaped lives in shell.js. This keeps the surface where three implementations can diverge as small as it can be.

## Principles

**The contract is the product; hosts are commodities.** Quality lives in the spec, the conformance suite, and the mock host. A host is done when the suite passes. Cross-platform consistency is won in error-code mapping and conformance tests.

**System WebView, and its consequences owned.** The OS renders; the contract absorbs what that implies. Engine version is checked at boot against a spec'd floor. Renderer crashes are recovered by rule, not by luck. WebView storage is declared cache — durable data belongs in `fs`/`path.appData` or in a durable adapter.

**Adapters are the app model.** An Engawa app is JS + adapters. An adapter is defined spec-first: spec.md and a conformance module in, per-platform implementations out. The N-platform implementation cost is accepted; the repo ships `/new-adapter` to scaffold it. Built-ins are in-tree adapters; there is no privileged dispatch path, so the adapter API is load-bearing from the first line.

**Two reference adapters ship with v1: `sqlite` and `update`. They are one claim, two faces.** SQLite guarantees data does not depend on the WebView's whims; update guarantees code does not depend on full-binary redistribution. Persistence of data and self-managed delivery of code are the two properties that separate a desktop app from a website — with both, Engawa is an app engine; with either alone, it is a shell. They are also technically complementary as adapter-API validation: sqlite is request-driven, local, synchronous-shaped; update is host-event-driven, long-running, network-facing, and ends in the self-referential act of the host replacing itself. An adapter API that carries both is trustworthy; one that carries only sqlite is half-tested.

Update speaks two modes over one manifest — **app-update** (signed asset swap, atomic, applies at relaunch) and **full-update** (base binary; the adapter delivers a verified installer and the handoff event, the OS executes the replacement). Compatibility between modes is decided by the same `capabilities` vocabulary the boot handshake uses. Trust (signature verification, embedded public key) is contract law; delivery (where manifests live, channels, polling) is adapter freedom.

**Sidecars below adapters.** App-specific native needs run as spawned processes over stdio JSON-RPC (`process.*`). Promotion path: recurring sidecar pattern → adapter repo → adapter index.

**Static composition.** The host is built per app, with the app's chosen adapters compiled in. Distribution of adapters is a git commit hash. No registry, no dynamic loading, no binary distribution. Which adapters are mandatory versus per-app is set out in **Composition** below.

**Monorepo.** A change touching spec + shell.js + conformance + host + adapter + example lands as one atomic commit; divergence among them is this architecture's failure mode. `adapters/sqlite/` mirrors the external adapter layout exactly and can be extracted verbatim.

**Two verification layers.** The conformance suite defines host conformance. `make notes` — build, bundle, launch `examples/notes`, write a record, relaunch, read it back, apply a signed update, scripted — is the acceptance gate: it covers packaging, entitlements, and real-bundle behavior that conformance cannot reach. One command is the entire acceptance procedure.

**Clean-room hosts.** Windows and Linux hosts are implemented from the frozen spec, shell.js, and the suite — the macOS host source is off-limits to the implementer. Spec holes are only visible to an implementer without reference-host knowledge; ambiguities resolve into the spec.

## Composition

**`update` is mandatory — it is part of what Engawa is.** Self-managed delivery of code is one of the two properties that make Engawa an app engine rather than a shell (above), and its trust and manifest rules (§7.1/§8) are host obligations. So `update` is contract-coupled: it versions with the contract, is never extracted, and is composed into *every* host. It is not an app's choice.

**Every other adapter is per app.** The reference `sqlite` is an ordinary, optional adapter — an app that wants durable SQL declares it (in `engawa.json`, see `cli/`) and it is compiled in; an app that declares nothing gets a host without it. `sqlite` is not part of Engawa the way `update` is; the original drift was baking it into every host as if it were.

**Composition is realized per host, by the CLI, from the same `engawa.json`.** macOS generates a per-app SwiftPM package depending on exactly the declared adapters. Windows generates a per-app CMake project via `hosts/windows/engawa-host.cmake` (`engawa_add_host`): each declared adapter contributes its `hosts/windows/*.cpp` and an optional `deps.cmake` (its native dependencies, e.g. the sqlite adapter fetches the SQLite amalgamation), and a generated compose TU registers each via the `make<Package>Adapter()` factory convention. Linux works the same way via `hosts/linux/engawa-host.cmake` (each adapter's `hosts/linux/*.cpp` + `deps.cmake`), sharing the generated compose TU and factory convention with Windows. A local adapter is referenced by path; an external one is fetched by git revision. So on any OS an app is built in its own environment — a Linux build never needs the macOS or Windows toolchain — and gets a host holding exactly its declared adapters plus the mandatory `update`.

`adapters/` holding both is expected, not a smell: `update` (mandatory, contract-coupled, never extracted) and `sqlite` (a reference adapter, extractable) are both adapters — only their obligation to the project differs.

## Known risks

| Risk | Position |
|------|----------|
| Contract under-specification | The central risk. Suite + mock host written first; ambiguities resolve into the spec, never into hosts. |
| `app://io` PUT-body streaming per engine | Verified against the real engine before the command set depends on it (WKURLSchemeHandler upload streaming is the least documented of the three). Fallback: chunked POST with a session token. |
| WebView engine divergence (rendering/JS features) | Out of contract scope. Engine matrix documented; apps feature-detect. |
| Spec freeze discipline | `contractVersion` negotiation in the handshake; additive-only within a major. |
| Governance | The maintainer decides unilaterally. The adapter promotion path implies a multi-implementer world; the tension is acknowledged and deferred until a second independent maintainer exists. |

## Why this project

1. **Spec primacy:** this class of software written as a specification, with implementations as its commodities.
2. **Constitutional restraint:** a shell that permanently refuses in-host custom commands, extending only through adapters and sidecars. The constraint is the design.
