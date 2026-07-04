# `notification` — system notifications

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `notification.show` | `{ title, body? }` | `null` | Posts a system notification. Missing/empty `title` → `EINVAL`. |

## Testability hook (normative, conformance only)

`notification.show` has an OS side effect and no readable result, so under **conformance** the
host records each request and exposes it via `notification.__recorded` (no args → array of
`{ title, body }` in call order). Absent in normal runs (→ `ENOSYS`). Same rationale as the §9
`ENGAWA_FAKE_ENGINE_VERSION` hook.

Errors: `EINVAL`, `ENOSYS`.
