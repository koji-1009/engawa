# `fs` — filesystem (text)

Text file and directory operations. **Text only** — binary rides the `app://io` channel
(contract §5a), not the message channel. Paths are absolute; `fs` is not sandboxed in v1
(contract §7 — the app author is trusted, remote content never reaches commands). Durable
data lives here, under a `path.*` directory (contract §10).

Every command takes an object with at least `path`. A missing/empty `path` is `EINVAL`.

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `fs.readTextFile` | `{ path }` | string | UTF-8 contents. `ENOENT` if absent, `EISDIR` if a directory. |
| `fs.writeTextFile` | `{ path, contents }` | `null` | Creates or overwrites `path`. The parent directory must exist (`ENOENT` otherwise). Write is atomic (temp + rename). |
| `fs.exists` | `{ path }` | boolean | True iff something exists at `path`. |
| `fs.mkdir` | `{ path, recursive? }` | `null` | Creates a directory. `recursive` creates parents and is idempotent. Non-recursive on an existing path → `EEXIST`. |
| `fs.remove` | `{ path, recursive? }` | `null` | Removes a file or directory. `ENOENT` if absent. A non-empty directory without `recursive` → `ENOTEMPTY`. |
| `fs.readDir` | `{ path }` | `[{ name, isDirectory }]` | Directory entries (order unspecified). `ENOTDIR` if `path` is not a directory. |
| `fs.stat` | `{ path }` | `{ type, size, modified }` | `type` is `"file"` or `"directory"`; `size` in bytes; `modified` in epoch milliseconds. |

Normative:

- `readTextFile`/`writeTextFile` are UTF-8. Content round-trips byte-for-byte, unicode included.
- `writeTextFile` does not create parent directories — that is `mkdir`'s job; this keeps a typo'd path from silently scattering directories.
- Entry order from `readDir` is unspecified; callers must not depend on it.

Errors: `EINVAL`, `ENOENT`, `EEXIST`, `EISDIR`, `ENOTDIR`, `ENOTEMPTY`, `EIO`, `ENOSYS`.
