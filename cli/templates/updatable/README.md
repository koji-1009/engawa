# {{name}} — a self-updating app

A minimal app with an "update status" panel. On load it calls `update.status` and renders the
current A/B slot, the running version, and whether an update is pending. Use this as the starting
point for an app that ships signed self-updates.

## What it shows

- The `update` namespace: `update.status` → `{ currentSlot, bootingSlot, version, hasPending,
  pendingSlot }` (see `adapters/update/spec.md`).
- That trust (contract §7.1) and the atomic asset-root swap (§8) are **host obligations** — the
  host verifies an ed25519 signature over the payload hash against a **baked-in** trust root
  before anything lands under `app://`, and swaps slots atomically. The app just reports and,
  once initialized on a pending slot, calls `update.confirmBoot` to adopt it.
- External JS only — inline `<script>` is dead under the default CSP (§7.3).

## Signing & the trust root

The publisher's ed25519 public key is compiled into the build; updates must be signed with the
matching private key or the host discards them (§7.1). To set this up for dev:

1. `engawa keygen` — writes `trust-root.txt` (public key, embedded in builds) and
   `engawa-signing.key` (private key — keep it secret; it signs your updates).
2. `engawa build` — bakes `trust-root.txt` into the host as the compiled-in trust root. There is
   no runtime trust-root file to swap; that is deliberate (§7.1).
3. Sign an update payload with `engawa sign <payload> --key engawa-signing.key --version <v>` to
   get `{ hash, signature }`, then deliver it (transport is out of contract scope, §8) and stage
   it with `update.stageAppUpdate`.

See contract §7.1 (trust) and §8 (A/B slots, compatibility, rollback), and
`adapters/update/spec.md` for the full command table.

## Next steps

- `engawa dev` — build (debug) and launch.
- `engawa build` — bundle a runnable app.
