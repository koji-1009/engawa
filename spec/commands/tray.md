# `tray` — status-area icon (per-app)

A **per-app** namespace (contract §3.1): composed only when the app declares `tray` in `engawa.json`.
It puts an icon in the OS status area (macOS menu bar extras, Windows notification area, Linux status
notifier) with a tooltip and a click menu, and reports clicks back as events. It is OS-UI, so — like
`dialog`/`notification` — under conformance it records intent and exposes testability hooks rather than
driving real UI (a real status item and real clicks are not headless-testable).

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `tray.set` | `{ tooltip?, menu? }` | `null` | Create or update the status item. `tooltip` is a string; `menu` is an array of `{ id, label }` items (an item with no `label` is a separator). The host shows a default icon. Calling `set` again replaces the tooltip/menu. |
| `tray.remove` | — | `null` | Remove the status item. A no-op if none is set. |

Events:

- `tray.clicked` — payload `null`. The status item itself was activated (clicked).
- `tray.menuClicked` — payload `{ id }`. A menu item with that `id` was chosen.

The tray does not manage the window: an app wires `tray.clicked` to `window.*` (e.g. show/minimize)
itself. A background app pairs `tray` with the host's dock-less activation.

## Testability hooks (normative, conformance only)

`tray.__click` (no args) and `tray.__menuClick` (`{ id }`) exist **only in conformance mode**. They
drive the exact same event path a real activation would — emitting `tray.clicked` / `tray.menuClicked`
— so the suite can exercise event delivery without synthesizing OS input. In normal runs they are
absent (→ `ENOSYS`). This mirrors the §9 `ENGAWA_FAKE_ENGINE_VERSION` and `window.requestClose` hooks.

Errors: `EINVAL` (malformed `menu`), `ENOSYS`.
