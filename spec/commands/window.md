# `window` — the application window

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `window.setTitle` | `{ title }` | `null` | Sets the window title. |
| `window.getSize` | — | `{ width, height }` | Content size in **CSS pixels** (device-independent; the same unit CSS sees, not physical/backing pixels). |
| `window.setSize` | `{ width, height }` | `null` | Sets the content size, in CSS pixels. Missing/non-number → `EINVAL`. A host MAY clamp non-positive values to its minimum. |
| `window.setResizable` | `{ resizable }` | `null` | Enables/disables user resizing. |
| `window.minimize` | — | `null` | Minimizes the window. |
| `window.maximize` | — | `null` | Maximizes / zooms the window. |
| `window.close` | — | `null` | Closes the window programmatically (does **not** emit `closeRequested`; that is for user-initiated closes). |
| `window.setCloseHandler` | `{ enabled }` | `null` | Opts in (`true`) or out (`false`, default) of intercepting user close attempts. |
| `window.respondToClose` | `{ token, allow }` | `null` | Answers a `window.closeRequested`. Unknown/consumed `token` → `EINVAL`. |

Events: `window.focus`, `window.blur`, `window.resize` (payload `{ width, height }`, coalesced per §2.1), `window.closeRequested` (payload `{ token }`).

## Close protocol (contract §4.2)

Interception is **opt-in**. By default a user close attempt closes the window — an app that
never calls `window.setCloseHandler(true)` is always closable (no unclosable-window footgun).

Once the app calls `window.setCloseHandler(true)`, a user close attempt no longer closes the
window: the host emits `window.closeRequested` with a `{ token }` and waits **indefinitely** (no
timeout — a slow unsaved-changes prompt must not become data loss). The app answers
`window.respondToClose(token, allow)`: `allow: true` closes the window, `allow: false` keeps it.
A token is single-use; answering twice → `EINVAL`. `setCloseHandler(false)` restores the default.

## Testability hook (normative, conformance only)

`window.requestClose` (no args) exists **only when the host runs in conformance mode**. It drives
the exact same close gate a real user close would and returns `{ deferred }` — `true` if the app
had opted in (a `closeRequested` was emitted), `false` if the window would just close — so the
suite can exercise both paths without synthesizing OS input or destroying its window. In normal
runs the command is absent (→ `ENOSYS`). This mirrors the §9 `ENGAWA_FAKE_ENGINE_VERSION` hook: a
test affordance the contract names so every host provides it identically, never a production feature.

Errors: `EINVAL`, `ENOSYS`.
