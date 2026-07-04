# adapters/update — contract-coupled adapter (self-managed updates)

v1's other face: **code does not depend on full-binary redistribution** (design.md). Host-event-driven, long-running, network-facing; ends in the host replacing itself. Speaks two modes over one manifest — app-update (signed atomic asset swap) and full-update (verified installer + handoff event) — per contract §8.

Not an ordinary adapter: §7.1 signature verification against the embedded trust root and the atomic asset-root swap are **host obligations** (CLAUDE.md). Versions with the contract; never extracted. Built in bootstrap stage 5, after §8 and §7.1 are real.
