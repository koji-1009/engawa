# engawa CLI

Scaffold, build, and run [Engawa](../) apps. An Engawa app is **JS + adapters** statically
composed with the reference host (WKWebView) into a codesigned `.app`.

## Requirements

- macOS 13+ with the Swift toolchain (Xcode) — for the host build — and `codesign`.
- **Node 24+**. The CLI is written in TypeScript and runs **directly** via Node's type
  stripping — there is no build step.
- The Engawa reference implementation, found via `ENGAWA_HOME` or auto-detected from the CLI's
  location.

## Run

```sh
node src/main.ts <command>          # from cli/
# or expose the `engawa` bin:
npm link                            # then: engawa <command>
```

## Commands

| Command | What it does |
|---------|--------------|
| `engawa new <name>` | Scaffold an app: `engawa.json` + `app/` (a notes demo over the sqlite adapter). |
| `engawa dev` | Build (debug) and launch the app. |
| `engawa build [--out <dir>] [--sign <identity>]` | Build the host, assemble a `.app` (Info.plist, `Resources/{app,shell.js,engawa.json,trust-root.txt}`), and codesign (ad-hoc by default). |
| `engawa keygen [--out <dir>]` | Generate a dev ed25519 signing keypair — `trust-root.txt` (embedded, contract §7.1) + `engawa-signing.key` (secret). |
| `engawa sign <payload> --key <key> [--version <v>]` | Sign an app-update payload → `{hash, signature}` for an update manifest (§7.1/§8). |
| `engawa info` | Contract/host versions and a toolchain check. |

## The app

`engawa.json`:

```json
{ "id": "dev.engawa.mynotes", "name": "mynotes", "version": "1.0.0", "adapters": [], "sidecars": [] }
```

`app/` holds the assets served from the `app://` origin. Inline `<script>` is dead under the
default CSP (§7.3) — use external scripts (`<script src="main.js">`). Durable data belongs in
`fs` or the `sqlite` adapter (§10), never WebView storage. Declared sidecars (§7.2) live in
`bin/` and are copied into the bundle.

## Adapters (static composition)

The host is composed **per app** (docs/design.md): every host has the built-in namespaces and
the contract-coupled `update` adapter, and `build` compiles in **only** the adapters the app
declares — nothing else is baked in. A scaffolded app declares none (it uses `fs`); an app that
wants `sqlite` (or any other adapter) declares it:

```json
"adapters": [
  { "package": "sqlite", "product": "EngawaSQLite", "register": "SqliteAdapter()", "path": "../../adapters/sqlite" }
]
```

- `package` — SwiftPM package identity; `product` — the library to import; `register` — the Swift
  expression that constructs the adapter.
- Reference it by **`path`** (relative to the app dir) or by **git**: `"url"` + `"revision"` (a
  commit hash — the design's distribution unit). Exactly one of the two.

`build` generates a SwiftPM package under `build/.host/` that depends on `EngawaHostCore` plus the
declared adapters, builds it, and bundles the resulting binary. See `examples/notes` (declares
`sqlite`) vs `examples/files` (core only).

## TypeScript

Written for **TypeScript 6**, forward-compatible with the TS 7 native compiler: ESM only,
`isolatedModules`, `verbatimModuleSyntax`, and erasable syntax (no enums / namespaces /
parameter properties) — which is exactly what lets Node run the sources without compiling.
Type-check with `npm run typecheck` (needs TypeScript 6).
