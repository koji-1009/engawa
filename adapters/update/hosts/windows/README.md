# adapters/update/hosts/windows — Windows native impl

The `update` namespace (adapters/update/spec.md) for the Windows host. Contract-coupled: this adapter
owns delivery + policy, but **trust** (§7.1 ed25519 verification against the embedded root) and the
**atomic A/B slot swap** (§8) are host obligations it delegates to `UpdateHost`
(`hosts/windows/src/UpdateHost.cpp`) — a host is non-conformant without them regardless of adapter presence.

Speaks two modes over one manifest (§8): app-update (signed atomic asset swap) and full-update
(verified installer + `update.readyToInstall`; the OS executes the replacement, out of contract scope).

Per §3 the sources live here; the Windows reference host compiles them in (static composition). ed25519
verification uses the vendored `tweetnacl` (`hosts/windows/third_party/`); the payload SHA-256 uses the
Windows CNG API (`bcrypt`).
