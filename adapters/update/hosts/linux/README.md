# adapters/update/hosts/linux — Linux native impl

The `update` namespace (adapters/update/spec.md) for the Linux host. Contract-coupled: this adapter
owns delivery + policy, but **trust** (§7.1 ed25519 verification against the compiled-in root) and the
**atomic A/B slot swap** (§8) are host obligations it delegates to `UpdateHost`
(`hosts/linux/src/UpdateHost.cpp`) — a host is non-conformant without them regardless of adapter presence.

Speaks two modes over one manifest (§8): app-update (signed atomic asset swap) and full-update
(verified installer + `update.readyToInstall`; the OS executes the replacement, out of contract scope).

Per §3 the sources live here; the Linux reference host compiles them in (static composition). The
adapter source is byte-identical to the Windows impl (it delegates all crypto/slot work to the host).
On Linux the host's SHA-256 + ed25519 verification is provided by libsodium (`hosts/linux/src/Crypto.cpp`).
