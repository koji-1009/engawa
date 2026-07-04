# `path` — standard directories

Resolves the per-app and per-user directories an app writes into. Every command takes no
args and returns an absolute path string. The per-app directories are created if absent, so
a returned path is always usable. Durable app data belongs here (contract §10), not in
WebView storage.

| Command | Returns | Meaning |
|---------|---------|---------|
| `path.appData` | string | Per-app durable data directory. Created if absent. |
| `path.appConfig` | string | Per-app configuration directory. Created if absent. |
| `path.appCache` | string | Per-app cache directory (evictable by intent, not by the engine). Created if absent. |
| `path.home` | string | The user's home directory. Not created; assumed to exist. |
| `path.temp` | string | A directory for temporary files. |

Normative:

- A returned path is absolute and stable within a run: repeated calls to the same command return the same path.
- The three per-app directories (`appData`, `appConfig`, `appCache`) are scoped to the app's identity and MUST NOT collide between apps. A host resolves them under the OS-conventional locations; the exact layout is host business, the guarantees above are not.
- No command here takes arguments. Unknown command in this namespace → `ENOSYS`.

Errors: `ENOSYS` (no such command). Directory creation failure surfaces as `EIO`.
