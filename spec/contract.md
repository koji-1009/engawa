# Engawa Contract

**Status: DRAFT.** This document grows with the reference implementation and freezes as `contract-1.0`. Until tagged, nothing here is stable. Ambiguities discovered during implementation are resolved *here*, never in host-specific behavior.

Keywords MUST / MUST NOT / SHOULD follow RFC 2119.

## 1. Boot handshake

The host injects, at document start, into every qualifying document (§6):

```js
window.__shell = {
  contractVersion: "1.0",
  platform: "macos" | "windows" | "linux",
  capabilities: ["window", "dialog", "fs", ...],  // namespaces this host serves
  postMessage(jsonString)          // JS → host, fire-and-forget
};
```

Host → JS delivery: the host evaluates `__shell._deliver(jsonArrayString)`. `_deliver` is defined by shell.js (§2); a host MUST NOT call it before shell.js has executed.

shell.js — the shared runtime library, identical bytes on every host — is injected immediately after `__shell`. It implements `invoke()`, promise correlation, and event subscription on top of the two host primitives. Hosts implement exactly two things: receive a string, evaluate a string.

### 1.1 Public runtime API (normative)

shell.js exposes the runtime as `globalThis.engawa` — the sole public surface app code uses. It is stable within a contract major version:

- `engawa.invoke(cmd, args) → Promise` — issues one request. Resolves with the response `value`; rejects with an `Error` whose `.code` is the error frame's `code` (§2) and whose `.message` is its `message`. `args` defaults to `null`.
- `engawa.on(topic, handler) → unsubscribe` — subscribes to an event topic (§4). Returns a function that removes the subscription; `engawa.off(topic, handler)` is equivalent.
- `engawa.contractVersion`, `engawa.platform`, `engawa.capabilities` — read-only copies of the handshake fields.

Injection is idempotent: re-running shell.js in a document that already has it (history traversal, double injection per §6) MUST NOT reset pending requests or subscriptions. shell.js signals this to itself via `__shell.__engawaLoaded`.

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

## 4. Built-in command set v1

| Namespace | Commands |
|-----------|----------|
| `window` | `setTitle`, `setSize`, `getSize`, `minimize`, `maximize`, `close`, `setResizable`, `respondToClose` |
| `dialog` | `open`, `save`, `message` |
| `fs` | `readTextFile`, `writeTextFile`, `exists`, `mkdir`, `remove`, `readDir`, `stat` — text only; binary via §5a |
| `path` | `appData`, `appConfig`, `appCache`, `home`, `temp` |
| `clipboard` | `readText`, `writeText` |
| `shellOpen` | `openExternal`, `revealInFolder` |
| `notification` | `show` |
| `process` | `spawn`, `stdinWrite`, `read`, `kill` |
| `app` | `version`, `engineInfo`, `quit` |

Events v1: `window.focus`, `window.blur`, `window.resize`, `window.closeRequested`, `process.readable`, `process.exit`, `app.renderCrashed`.

Per-command normative details (args, results, error codes) are specified in `spec/commands/<namespace>.md`, one file per namespace, written in the same commit as the implementation and its conformance test.

### 4.1 `process` streams (normative)

Streams are pull: `process.readable` signals data availability (coalesced, no payload); JS drains with `process.read(pid, stream, maxBytes)`. The host buffers up to 8 MiB per stream, then applies OS backpressure by pausing pipe reads. `process.exit` fires only after both streams are drained.

### 4.2 `window.closeRequested` (normative)

On a user close attempt the host emits `window.closeRequested` with `payload: { token }` and waits **indefinitely**. JS answers through the ordinary request path: `window.respondToClose(token, allow)` — no special frame type exists for event replies. An unknown or already-consumed token is `EINVAL`. No timeout: the canonical use is an unsaved-changes dialog, and a timeout converts a slow user decision into data loss. A hung page is the OS's problem (repeated close, force quit). Hosts MUST NOT invent a deadline.

## 5. Asset serving

Custom scheme `app://` on all platforms. Never `http://localhost`. The host serves the app's asset directory with correct MIME types. Root resolution and 404 behavior: `spec/assets.md`.

### 5a. Binary I/O channel

Binary never travels the message channel. It rides the scheme handler:

- **Read:** `fs.openRead(path)` → `{ url: "app://io/<token>" }`; JS fetches a streamed body. Token single-use, expires on completion or 30 s idle.
- **Write:** `fs.openWrite(path)` → `{ url: "app://io/<token>" }`; JS `fetch(url, { method: "PUT", body })`; the PUT response body carries the result/error frame as JSON.
- `app://io/*` is reserved; asset serving MUST NOT collide.
- `app://io` is a **distinct origin** from the app's asset origin (§5). A host MUST attach response headers that let the app page read `app://io` responses — `Access-Control-Allow-Origin` covering the app origin — and MUST answer a CORS preflight (`OPTIONS`) on `app://io` should the engine issue one. Without this the app cannot read the result/error frame the PUT returns.

> Verified (bootstrap stage 2 spike): on the reference engine the PUT request body reaches the scheme handler intact (`URLRequest.httpBody`), so the message channel is never touched for binary. The design.md fallback (chunked POST + session token) is therefore unneeded. The only refinement the spike forced is the CORS requirement above — the app origin and `app://io` differ, so the response must opt the app origin in.

## 6. Injection semantics (normative)

- `__shell` + shell.js are injected at document start into **top-level `app://` documents only**.
- Never into iframes (any origin), `http(s)://` documents, or `about:blank`.
- Injection MUST precede execution of any document script, on every navigation including reload and history traversal.

## 7. Security model

- v1 threat model is remote content, not the app author. Commands are all-on for `app://` pages.
- Any `http(s)://` navigation gets a dead `__shell` (postMessage no-ops). External-origin navigation is denied by default; `shellOpen.openExternal` is the sanctioned path.
- `fs` paths are NOT sandboxed in v1: the app author is trusted; remote content never reaches commands (§6).

### 7.1 Update trust model (normative)

What `app://` loads is a constitutional matter; delivery mechanics are an adapter's business, trust is not:

- The host embeds the app publisher's public key (ed25519) at build time. It is the trust root.
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
    "contractRequired": "1.0",
    "capabilitiesRequired": ["sqlite", "update"],
    "url": "...", "hash": "sha256-...", "signature": "ed25519-..."
  },
  "base": {
    "version": "1.2.0",
    "contractProvided": "1.0",
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

- **app-update:** verified payload unpacks to a fresh directory; the `app://` root switches atomically (rename/symlink); applies at next launch; failure leaves the previous root untouched — rollback is the default state, not a mechanism.
- **full-update:** the update adapter's obligation ends at "verified installer on disk + `update.readyToInstall` event." Installation executes OS-natively when the app calls `update.install` (host exits into the platform's replace flow: .app swap, installer launch, AppImage replace). The contract specifies everything up to that handoff; nothing after it.

## 9. Platform baseline

- OS floor: macOS 13+, Windows 10 1809+ (WebView2 Evergreen), Ubuntu 22.04+ / webkit2gtk-4.1 equivalents. Raising a floor is a contract minor version.
- Engine check at boot: below the spec minimum → spec'd error screen showing detected/required versions; no partial boot. `app.engineInfo` exposes engine name/version, host version, contract version.
- Testability hook (normative): a host MUST honor `ENGAWA_FAKE_ENGINE_VERSION` (env) by substituting it for the detected engine version. This exists solely so the conformance suite can exercise floor rejection on any machine; it is not a production feature and MUST NOT bypass §7.1.

## 10. Runtime resilience (normative)

- **Renderer crash recovery.** A host MUST reload on renderer-process death and emit `app.renderCrashed` (with crash counter) after recovery. Silent white screens are non-conformant. Three crashes in 60 s → spec'd error screen instead of a reload loop.
- **Storage durability.** WebView-managed storage (IndexedDB, localStorage, caches) is cache: engines may evict it and the contract does not fight them. Durable data belongs in `fs` under `path.appData` or in a durable-storage adapter (e.g. `sqlite`). The conformance suite wipes WebView storage between runs and asserts the app still boots.

## 11. Conformance

A host is conformant when the suite (`/conformance`) passes, including: every command and every spec'd error code; flow-control batching and coalescing; `process.read` backpressure under a firehose sidecar; `closeRequested` with a slow responder; §5a round-trips at 1 KiB / 10 MiB / 100 MiB; §6 presence/absence matrix; renderer-kill recovery; boot-after-storage-wipe; engine floor rejection; unicode integrity; 1 MiB message survival.

The suite also runs against the Node mock host. Any requirement the mock host cannot satisfy without emulating a specific engine is an engine-ism smuggled into this contract, and is a spec bug.
