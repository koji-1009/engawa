# `update` adapter — self-managed updates

Normative spec for the `update` namespace. **Contract-coupled** (CLAUDE.md): this adapter owns
delivery and policy, but trust (contract §7.1) and the atomic A/B slot swap (§8) are **host
obligations** it delegates to — a host is non-conformant without them regardless of adapter
presence. Speaks two modes over one manifest (§8): app-update (signed asset swap) and full-update
(base binary; the adapter delivers a verified installer and the handoff event, the OS executes it).

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `update.status` | — | `{ currentSlot, bootingSlot, version, hasPending }` | Current A/B slot state. |
| `update.evaluate` | `{ manifest, provided }` | `{ mode, version }` | §8 compatibility rule: `mode` is `"app-update"` if the running base (`provided.contractProvided` + `provided.capabilities`) satisfies `manifest.app.contractRequired`/`capabilitiesRequired`, else `"full-update"` (and `update.readyToInstall` is emitted). |
| `update.stageAppUpdate` | `{ payloadPath, hash, signature, version }` | `{ staged }` | Host verifies (§7.1) then unpacks into the non-live slot and reserves adoption (`pending`, the single atomic commit point). Bad hash → `EHASH`; bad/absent signature → `ESIGNATURE`. |
| `update.confirmBoot` | — | `null` | The app initialized on the pending slot: adopt it (§8). **Required** — an app that never calls it cannot be updated. |
| `update.install` | — | `{ handoff }` | full-update handoff; the OS-native replacement is out of contract scope (§8). |

Events: `update.readyToInstall` (payload `{ version }`) — a verified installer is available (full-update).

## A/B slots (contract §8, host obligation)

`pending` is written by a single atomic file write — the only commit point; at any crash instant
the state is "pre-update" or "adoption reserved", never in between. On next launch the host boots
the pending slot and bumps `health.attempts`; `confirmBoot` adopts it. If `confirmBoot` does not
arrive within **2 launch attempts**, the host discards `pending` and boots the previous slot — so
rollback covers not just interrupted swaps but **verified-yet-broken payloads** (valid signature,
fails to boot). The conformance suite drives relaunches via the `update.__relaunch` testability
hook (conformance only; re-runs the boot-time slot decision without restarting the process).

## Trust (contract §7.1, host obligation)

The host embeds the publisher's ed25519 public key at build time (dev builds: a Makefile-generated
keypair). A payload MUST carry a signature over its content hash, verified against that key
**before** any file is placed under the `app://` root. Unverified payloads are discarded — no
override. Manifest transport is out of contract.

Errors: `EINVAL`, `ENOENT`, `EHASH`, `ESIGNATURE`, `ENOSYS`.
