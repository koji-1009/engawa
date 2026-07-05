# `app` — application lifecycle & identity

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `app.version` | — | string | The app's own version (from its manifest / build). |
| `app.engineInfo` | — | `{ engine, engineVersion, hostVersion, contractVersion }` | The WebView engine, the host build, and the contract version in force (contract §9). All string fields. |
| `app.quit` | — | `null` | Requests orderly application termination. Returns before the process exits. |

Events:

- `app.renderCrashed` — payload `{ count }`. Emitted after the host recovers from a renderer-process crash (contract §10): the WebView is reloaded and this fires with the running crash count (from 1). Three crashes within 60 s trip a spec'd error screen instead of another reload.

Normative:

- `app.engineInfo.contractVersion` MUST equal `engawa.contractVersion` (§1.1) — one source of truth for the running contract.
- `app.quit` terminates the app; a well-behaved host runs its normal shutdown (windows may still receive `window.closeRequested` per §4.2 if the app wired it). Because it ends the process, the conformance suite specs it but does not invoke it — the `make notes` gate exercises quit/relaunch end to end.

Errors: `ENOSYS`.
