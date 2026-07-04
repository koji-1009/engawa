# `shellOpen` — hand off to the OS

The sanctioned path for leaving the app: opening a URL in the user's browser or revealing a
file in the file manager (contract §7 — external-origin navigation is otherwise denied).

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `shellOpen.openExternal` | `{ url }` | `null` | Opens `url` in the default handler. Missing/empty `url` → `EINVAL`. |
| `shellOpen.revealInFolder` | `{ path }` | `null` | Reveals `path` in the file manager. Missing/empty `path` → `EINVAL`. |

## Testability hook (normative, conformance only)

These commands have OS side effects and no readable result, so in **conformance mode** a host
records each request instead of performing it, and exposes the record via `shellOpen.__recorded`
(no args → array of `{ action, ... }` in call order). Absent in normal runs (→ `ENOSYS`). This
lets the suite assert intent without launching a browser. Same rationale as the §9
`ENGAWA_FAKE_ENGINE_VERSION` hook.

Errors: `EINVAL`, `ENOSYS`.
