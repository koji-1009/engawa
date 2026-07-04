---
description: Scaffold a new Engawa adapter (1 adapter = 1 namespace = 1 repo) from the contract layout.
---

# /new-adapter

Scaffold a new Engawa adapter. An adapter serves **exactly one namespace** over the wire protocol (contract §3) — same frames, same flow control, same error model as a built-in. The adapter author never touches IPC.

Namespace to scaffold: **$ARGUMENTS** (e.g. `sqlite`, `keychain`, `http`). If empty, ask for it before doing anything.

## Read first

- `spec/contract.md` §2 (wire protocol), §3 (adapters), §2.1 (flow control) — the invariants every adapter inherits.
- `spec/errors.md` — the error registry the new namespace's codes must fit into.
- `adapters/sqlite/` — the reference adapter; mirror its layout and conventions exactly. It is the worked example.

## Produce the layout (contract §3)

```
adapters/<namespace>/            # or a standalone repo — identical layout
  spec.md                        # normative: commands, events, error codes for this namespace
  conformance/                   # JS test module the suite runner loads
  js/                            # optional typed wrapper over invoke()
  hosts/{macos,windows,linux}/   # native implementations; partial platform coverage is legal
```

## Rules to enforce while scaffolding

- **Spec-first.** Write `spec.md` (commands, events, error codes) and the `conformance/` module before any host implementation. Commands land as spec + conformance + implementation in one commit (CLAUDE.md).
- **One namespace.** The adapter's every command is `<namespace>.<command>`. No cross-namespace dispatch, no privileged path.
- **Error codes** map into `spec/errors.md`; reuse existing codes for existing conditions rather than inventing synonyms.
- **No IPC.** Native code implements the `Adapter` protocol (`namespace`, `handle`, `attach`) only; the host owns the wire.
- **Distribution** is a commit-hash-pinned source dependency — no registry, no dynamic loading (contract §3).
- Binary payloads ride `app://io` (§5a), never the message channel; events carry signals, not payloads (§2.1).

Scaffold empty-but-valid stubs (a `spec.md` skeleton with the namespace's command table, an empty conformance module wired into the runner, per-platform host directories), then report what remains for the author to fill in.
