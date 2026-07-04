# `window` — the application window

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `window.setTitle` | `{ title }` | `null` | Sets the window title. |
| `window.getSize` | — | `{ width, height }` | Content size in points. |
| `window.setSize` | `{ width, height }` | `null` | Sets the content size. Missing/non-number → `EINVAL`. |
| `window.setResizable` | `{ resizable }` | `null` | Enables/disables user resizing. |
| `window.minimize` | — | `null` | Minimizes the window. |
| `window.maximize` | — | `null` | Maximizes / zooms the window. |
| `window.close` | — | `null` | Closes the window programmatically (does **not** emit `closeRequested`; that is for user-initiated closes). |
| `window.respondToClose` | `{ token, allow }` | `null` | Answers a `window.closeRequested`. Unknown/consumed `token` → `EINVAL`. |

Events: `window.focus`, `window.blur`, `window.resize` (payload `{ width, height }`, coalesced per §2.1), `window.closeRequested` (payload `{ token }`).

## Close protocol (contract §4.2)

A user close attempt does not close the window. The host emits `window.closeRequested` with a
`{ token }` and waits **indefinitely** (no timeout — a slow unsaved-changes prompt must not
become data loss). The app answers `window.respondToClose(token, allow)`: `allow: true` closes
the window, `allow: false` keeps it. A token is single-use; answering twice → `EINVAL`.

## Testability hook (normative, conformance only)

`window.requestClose` (no args → `null`) exists **only when the host runs in conformance mode**.
It drives the exact same close protocol a real user close would, so the suite can exercise
`closeRequested` → `respondToClose` without synthesizing OS input. In normal runs the command is
absent (→ `ENOSYS`). This mirrors the §9 `ENGAWA_FAKE_ENGINE_VERSION` hook: a test affordance the
contract names so every host provides it identically, never a production feature.

Errors: `EINVAL`, `ENOSYS`.
