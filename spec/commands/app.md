# `app` — application lifecycle & identity

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `app.version` | — | string | The app's own version (from its manifest / build). |
| `app.engineInfo` | — | `{ engine, engineVersion, hostVersion, contractVersion }` | The WebView engine, the host build, and the contract version in force (contract §9). All string fields. |
| `app.quit` | — | `null` | Requests orderly application termination. Returns before the process exits. |

Normative:

- `app.engineInfo.contractVersion` MUST equal `engawa.contractVersion` (§1.1) — one source of truth for the running contract.
- `app.quit` terminates the app; a well-behaved host runs its normal shutdown (windows may still receive `window.closeRequested` per §4.2 if the app wired it). Because it ends the process, the conformance suite specs it but does not invoke it — the `make notes` gate exercises quit/relaunch end to end.

Errors: `ENOSYS`.
