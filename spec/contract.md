# Engawa Contract

**Status: DRAFT.** This document grows with the reference implementation and freezes as `contract-1.0.0`. Until tagged, nothing here is stable. Ambiguities discovered during implementation are resolved *here*, never in host-specific behavior.

Keywords MUST / MUST NOT / SHOULD follow RFC 2119.

## 1. Boot handshake

The host injects, at document start, into every qualifying document (§6):

```js
window.__shell = {
  contractVersion: "0.1.0",   // DRAFT — pre-1.0 semver until the contract is frozen
  platform: "macos" | "windows" | "linux",
  capabilities: ["window", "dialog", "fs", ...],  // namespaces this host serves
  postMessage(jsonString)          // JS → host, fire-and-forget
};
```

Host → JS delivery: the host evaluates `__shell._deliver(jsonArrayString)`. `_deliver` is defined by shell.js (§2); a host MUST NOT call it before shell.js has executed.

shell.js — the shared runtime library, identical bytes on every host — is injected immediately after `__shell`. It implements `invoke()`, promise correlation, and event subscription on top of the two host primitives. Hosts implement exactly two things: receive a string, evaluate a string.

### 1.1 Public runtime API (normative)

`__shell` is the private host bridge; apps do not touch it. shell.js exposes the public surface as `globalThis.engawa`:

```js
engawa.invoke(cmd, args) → Promise      // rejects with { code, message } per §2
engawa.on(topic, handler) → unsubscribe // event subscription
engawa.platform                          // "macos" | "windows" | "linux"
engawa.capabilities                      // frozen array of served namespaces
engawa.contractVersion
```

Only namespaces present in `capabilities` are invokable; invoking an absent namespace rejects with `ENOTSUP` without a round-trip to the host. The `engawa` object is frozen after injection. Direct use of `__shell` by app code is unsupported and may break within a major version.

## 2. Wire protocol

JSON frames over the string channel:

```
{ "t": "req", "id": 42, "cmd": "fs.readTextFile", "args": {...} }
{ "t": "res", "id": 42, "ok": true,  "value": {...} }
{ "t": "res", "id": 42, "ok": false, "err": { "code": "ENOENT", "message": "..." } }
{ "t": "evt", "topic": "window.focus", "payload": {...} }
```

- `id` correlation is owned by shell.js. Hosts treat requests independently; out-of-order completion is expected. All commands are async.
- Error codes are enumerated per command in §4. Hosts MUST map platform errors to these codes. (Registry: `spec/errors.md` — grows with implementation.)

### 2.1 Flow control (normative)

Delivery via eval is expensive; the contract forbids unbounded push:

- `__shell._deliver` takes a JSON **array** of frames. A host MUST drain its outbound queue into at most one eval per main-loop tick.
- High-frequency window events (`window.resize`) MUST be coalesced: latest payload per batch only.
- Bulk data streams are pull-based, never events (§4 `process`, §5a). Events carry signals, not payloads.

## 3. Capabilities and adapters

- `capabilities` lists every namespace this host build serves, built-in and adapter alike. shell.js exposes present namespaces only.
- An adapter serves one namespace over this wire protocol — same frames, same flow control, same error model. Adapter authors never touch IPC.
- Adapter unit and distribution: 1 adapter = 1 namespace = 1 git repository, layout:

```
<adapter>/
  spec.md          # normative: commands, events, error codes
  conformance/     # JS test module for the suite runner
  js/              # optional typed wrapper over invoke()
  hosts/{macos,windows,linux}/   # native implementations; partial coverage is legal
```

- Distribution is commit-hash-pinned source dependency (SwiftPM exact-revision / CMake FetchContent GIT_TAG / C# project reference). No registry. Binding is static at app build time; hosts are built per app. Dynamic loading is out of contract.
- Host-side adapter API: each host exposes a registration interface in its own language with this shape (normative per host spec):

```swift
protocol Adapter {
  var namespace: String { get }
  func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue
  func attach(_ emitter: EventEmitter)
}
```

Built-in namespaces (§4) are adapters that ship in-tree. There is no privileged dispatch path.

### 3.1 Composition (normative)

A namespace is either **mandatory** — composed into every host — or **per-app** — composed only when the app declares it in its manifest (`engawa.json`). The mandatory core is exactly `app`, `window`, `update`, and `path`: identity/lifecycle (§1.1 handshake), the window, the update mechanism (a host obligation, §7.1/§8), and app-data path resolution. Every other namespace — `fs`, `notification`, `clipboard`, `dialog`, `shellOpen`, `process`, and every external adapter — is **per-app**: an app that does not declare it gets a host that does not serve it, and does not carry any OS permission or entitlement it would imply. `echo` exists for conformance only and MUST NOT be composed into a production host.

`capabilities` MUST equal exactly the composed set: a host MUST NOT advertise a namespace it has not composed, and MUST NOT serve one it has not advertised. Because §1.1 gates invocation on `capabilities`, invoking an undeclared namespace rejects locally with `ENOTSUP` and never reaches the host. Composition is static, at app build time; whether a host still carries the unreached code of an undeclared namespace is an implementation detail, but it MUST NOT be reachable or advertised.

## 4. Built-in command set v1

| Namespace | Commands |
|-----------|----------|
| `window` | `setTitle`, `setSize`, `getSize`, `minimize`, `maximize`, `close`, `setResizable`, `setCloseHandler`, `respondToClose` |
| `dialog` | `open`, `save`, `message` |
| `fs` | `readTextFile`, `writeTextFile`, `exists`, `mkdir`, `remove`, `readDir`, `stat`, `openRead`, `openWrite` — text direct; binary via `openRead`/`openWrite` over §5a |
| `path` | `appData`, `appConfig`, `appCache`, `home`, `temp` |
| `clipboard` | `readText`, `writeText` |
| `shellOpen` | `openExternal`, `revealInFolder` |
| `notification` | `show` |
| `process` | `spawn`, `stdinWrite`, `stdinClose`, `read`, `kill` |
| `app` | `version`, `engineInfo`, `quit` |

Events v1: `window.focus`, `window.blur`, `window.resize`, `window.closeRequested`, `process.readable`, `process.exit`, `app.renderCrashed`.

Per-command normative details (args, results, error codes) are specified in `spec/commands/<namespace>.md`, one file per namespace, written in the same commit as the implementation and its conformance test.

### 4.1 `process` streams (normative)

Streams are pull: `process.readable` signals data availability (coalesced, no payload); JS drains with `process.read(pid, stream, maxBytes)`. The host buffers up to 8 MiB per stream, then applies OS backpressure by pausing pipe reads. `process.exit` fires only after both streams are drained.

### 4.2 `window.closeRequested` (normative)

Close interception is **opt-in**. By default a user close attempt closes the window — an app that never asks to intervene is always closable. An app that must intervene (the canonical case is an unsaved-changes prompt) calls `window.setCloseHandler(true)` first. Only then does a user close attempt emit `window.closeRequested` with `payload: { token }` and wait **indefinitely** instead of closing; JS answers through the ordinary request path: `window.respondToClose(token, allow)` — no special frame type exists for event replies. An unknown or already-consumed token is `EINVAL`. No timeout on the wait: a timeout converts a slow user decision into data loss, and a hung page is the OS's problem (repeated close, force quit) — hosts MUST NOT invent a deadline. `window.setCloseHandler(false)` returns the window to the default (always-close) behavior.

## 5. Asset serving

Custom scheme `app://` on all platforms. Never `http://localhost`. The host serves the app's asset directory with correct MIME types. Root resolution and 404 behavior: `spec/assets.md`.

### 5a. Binary I/O channel

Binary never travels the message channel. It rides the scheme handler:

- **Read:** `fs.openRead(path)` → `{ url: "app://io/<token>" }`; JS fetches a streamed body. Token single-use, expires on completion or 30 s idle.
- **Write:** `fs.openWrite(path)` → `{ url: "app://io/<token>" }`; JS `fetch(url, { method: "PUT", body })`; the PUT response body carries the result/error frame as JSON.
- `app://io/*` is reserved; asset serving MUST NOT collide.
- **Origin and CORS (normative):** engines differ in whether custom-scheme URLs share an origin. `app://io` responses MUST carry `Access-Control-Allow-Origin` matching the app's `app://` origin (and `Access-Control-Allow-Methods: GET, PUT` on preflight where the engine issues one), so `fetch` from app documents succeeds regardless of the engine's custom-scheme origin policy. `spec/assets.md` defines the exact header set per response class.
- Spike outcome (recorded): WKURLSchemeHandler PUT request bodies stream correctly on the macOS floor; §5a stands as specified and the chunked-POST fallback is retired.

## 6. Injection semantics (normative)

- `__shell` + shell.js are injected at document start into **top-level `app://` documents only**.
- Never into iframes (any origin), `http(s)://` documents, or `about:blank`.
- Injection MUST precede execution of any document script, on every navigation including reload and history traversal.

## 7. Security model

- v1 threat model is remote content and injected script, not the app author. Commands are available to `app://` pages, subject to §7.2 and §7.3.
- Any `http(s)://` navigation gets a dead `__shell` (postMessage no-ops). External-origin navigation is denied by default; `shellOpen.openExternal` is the sanctioned path.
- `fs` paths are NOT sandboxed in v1: the app author is trusted; remote content never reaches commands (§6).

### 7.2 Sidecar allowlist (normative)

`process.spawn` launches only executables declared in the app manifest (`engawa.json`, `sidecars: ["./bin/..."]`) and resolved inside the app bundle. Arbitrary-path execution does not exist in this contract. An undeclared or out-of-bundle target is `EPERM`. The declaration is fixed at app build time, consistent with static composition.

### 7.3 Default CSP (normative)

The host injects a Content-Security-Policy on every `app://` response: `default-src app:; script-src app:` at minimum (CSP scheme-sources are unquoted — `app:`, not `'app:'`; the quoted form is invalid and blocks the app's own scripts too, not just inline). Relaxations (e.g. `connect-src` for API calls, `img-src` for remote media) are declared explicitly in `engawa.json` and applied verbatim; there is no silent widening. Inline script is dead by default — conformance asserts it. Host injection of `__shell` and shell.js (§1, §6) uses the engine's native user-script path (e.g. document-start user scripts), which is not subject to the page CSP; the CSP governs document content, not the host's own bridge.

### 7.1 Update trust model (normative)

What `app://` loads is a constitutional matter; delivery mechanics are an adapter's business, trust is not:

- The trust root is the app publisher's ed25519 public key, bound to the host at build time. A host MUST NOT source it at runtime from a mutable location beside the distributable (a loose file, a writable registry entry) — a swappable trust root could be replaced to authorize a malicious update, defeating the guarantee below. A host not delivered inside an OS-signed container that covers its assets MUST compile the key into its binary; one so delivered MAY instead carry it within that container. (The reference CLI compiles the key into the Windows host at build time; the macOS host carries it in its codesigned bundle.) An environment-variable trust root is permitted for development and conformance only, where no update crosses a real trust boundary.
- An app-asset payload (app-update) MUST carry a signature over its content hash, verified by the host against the embedded key **before** any file is placed under the `app://` root. Unverified payloads are discarded; there is no override.
- A base payload (full-update) MUST be verified the same way before the host announces it as installable. OS-level signing (codesign/notarization, Authenticode) applies on top, per platform.
- Update manifests (§8) are authenticated by the signatures of the payloads they point to; manifest transport (HTTPS, registry, file share) is out of contract.

## 8. Update manifest (normative)

One manifest serves both update modes; splitting them would let an app update outrun a base incompatibility unnoticed.

```json
{
  "manifestVersion": 1,
  "app": {
    "version": "2.3.1",
    "contractRequired": "0.1.0",
    "capabilitiesRequired": ["sqlite", "update"],
    "url": "...", "hash": "sha256-...", "signature": "ed25519-..."
  },
  "base": {
    "version": "1.2.0",
    "contractProvided": "0.1.0",
    "platforms": {
      "macos":   { "url": "...", "hash": "...", "signature": "...", "minOS": "13.0" },
      "windows": { "url": "...", "hash": "...", "signature": "...", "minOS": "10.0.17763" },
      "linux":   { "url": "...", "hash": "...", "signature": "...", "webkitgtk": "4.1" }
    }
  },
  "channel": "stable"
}
```

Compatibility rule: if the running base satisfies `app.contractRequired` and serves every namespace in `app.capabilitiesRequired`, an **app-update** applies alone (asset swap). Otherwise a **full-update** is required: base first, app after base success. The `capabilities` vocabulary of §1 is deliberately reused — runtime feature detection and update compatibility speak the same words.

Mode semantics:

- **app-update — A/B slots (normative).** The `app://` root is an indirection, not a directory:

```
<appData>/engawa/
  slots/{a,b}/     # asset trees; exactly one is live
  current          # pointer to the live slot (symlink or one-line file)
  pending          # slot awaiting adoption, if any
  health           # { bootingSlot, attempts }
```

  Flow: the verified payload (§7.1) unpacks into the non-live slot → `pending` is written (a single atomic file write — the only commit point; at any crash instant the state is either "pre-update" or "adoption reserved", never in between) → on next launch the host boots the pending slot and increments `health.attempts` → **the app** calls `update.confirmBoot()` once it has successfully initialized (this is app knowledge — shell.js cannot know when an app-defined "ready" is reached — so the app, not shell.js, is responsible for calling it) → the host switches `current`, clears `pending` and `health`. If `confirmBoot` does not arrive within 2 launch attempts, the host discards `pending` and boots the previous slot. This makes rollback cover not just interrupted swaps but **verified-yet-broken payloads**: a payload whose signature is valid but whose app fails to boot is automatically abandoned. Staging MUST reset the rollback budget: the host clears `health` as it stages a payload (before `pending` is written), so a freshly staged payload always begins with the full 2 attempts regardless of any earlier pending's accumulated `attempts` — otherwise a new, unrelated payload staged over a partially-failed one would inherit its diminished budget and be rolled back after fewer than 2 launches. `update.confirmBoot` is a required command of the `update` namespace; an app that never calls it cannot be updated.

- **full-update:** the update adapter's obligation ends at "verified installer on disk + `update.readyToInstall` event." Installation executes OS-natively when the app calls `update.install` (host exits into the platform's replace flow: .app swap, installer launch, AppImage replace). The contract specifies everything up to that handoff; nothing after it. On the first launch after a base replacement, the host MUST compare its `contractProvided` against the live slot's `contractRequired` and adopt a compatible `pending` app slot first if the live one no longer fits — completing the base-first/app-second ordering at boot.

## 9. Platform baseline

- OS floor: macOS 13+, Windows 10 1809+ (WebView2 Evergreen), Ubuntu 22.04+ / webkit2gtk-4.1 equivalents. Raising a floor is a contract minor version.
- Engine check at boot: below the spec minimum → spec'd error screen showing detected/required versions; no partial boot. The check MUST **fail closed**: if the host cannot determine the engine version at all (the version is unreadable, missing, or unparseable), it MUST treat that as below-floor and route to the same rejection screen (reporting the detected version as e.g. `unknown`) — it MUST NOT assume a version at or above the floor and boot. `app.engineInfo` exposes engine name/version, host version, contract version. The engine minimum is expressed in the engine's own version scheme, so it is per-engine: the reference hosts use WebKit `605.1.15` (macOS), Chromium `90.0.0.0` (Windows/WebView2), and WebKitGTK `2.28.0` (Linux/webkit2gtk-4.1 — the GTK3 API baseline; the engine version is WebKitGTK's own 2.x scheme, so a Chromium-style floor would spuriously reject every build). Because the scheme differs per engine, the conformance floor test draws its below/above straddle versions from the host driver (`engawa.engineFloorSamples`) rather than a fixed pair.
- Testability hook (normative): a host MUST honor `ENGAWA_FAKE_ENGINE_VERSION` (env) by substituting it for the detected engine version. This exists solely so the conformance suite can exercise floor rejection on any machine; it is not a production feature and MUST NOT bypass §7.1.

## 10. Runtime resilience (normative)

- **Renderer crash recovery.** A host MUST reload on renderer-process death and emit `app.renderCrashed` with `payload: { count }` (the running crash count, from 1) after recovery. Silent white screens are non-conformant. Three crashes in 60 s → spec'd error screen instead of a reload loop.
- **Storage durability.** WebView-managed storage (IndexedDB, localStorage, caches) is cache: engines may evict it and the contract does not fight them. Durable data belongs in `fs` under `path.appData` or in a durable-storage adapter (e.g. `sqlite`). The conformance suite wipes WebView storage between runs and asserts the app still boots.

## 11. Conformance

A host is conformant when the suite (`/conformance`) passes, including: every command and every spec'd error code; flow-control batching and coalescing; `process.read` backpressure under a firehose sidecar; `closeRequested` with a slow responder; §5a round-trips at 1 KiB / 10 MiB / 100 MiB; §6 presence/absence matrix; renderer-kill recovery; boot-after-storage-wipe; engine floor rejection; unicode integrity; 1 MiB message survival.

The suite also runs against the Node mock host. Any requirement the mock host cannot satisfy without emulating a specific engine is an engine-ism smuggled into this contract, and is a spec bug.
