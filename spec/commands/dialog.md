# `dialog` — native file & message dialogs

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `dialog.open` | `{ title?, multiple?, directory?, filters? }` | `{ canceled, paths }` | File/folder open panel. `paths` is the selection (empty when canceled). |
| `dialog.save` | `{ title?, defaultName? }` | `{ canceled, path }` | Save panel. `path` is the chosen path (`null` when canceled). |
| `dialog.message` | `{ message, title?, buttons? }` | `{ button }` | Message box. `button` is the index of the clicked button (0-based; default buttons `["OK"]`). Missing `message` → `EINVAL`. |

`filters` (open) is an array of `{ name, extensions: [".txt", ...] }`.

## Testability hook (normative, conformance only)

Dialogs are modal and user-driven, so in **conformance mode** the host does not present UI. It
returns a preprogrammed response set by `dialog.__setResponse(response)` (the next dialog command
returns `response` verbatim), defaulting to a canceled result if none is set. Argument validation
(e.g. `message` required) still applies. Absent in normal runs (→ `ENOSYS`). Same rationale as the
§9 `ENGAWA_FAKE_ENGINE_VERSION` hook.

Errors: `EINVAL`, `ENOSYS`.
