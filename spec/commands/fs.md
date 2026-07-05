# `fs` — filesystem (text)

Text file and directory operations. **Text only** — binary rides the `app://io` channel
(contract §5a), not the message channel. Paths are absolute; `fs` is not sandboxed in v1
(contract §7 — the app author is trusted, remote content never reaches commands). Durable
data lives here, under a `path.*` directory (contract §10).

Every command takes an object with at least `path`. A missing/empty `path` is `EINVAL`; so is a
**relative** `path` — paths must be absolute (a GUI app has no well-defined cwd to resolve against).

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `fs.readTextFile` | `{ path }` | string | UTF-8 contents. `ENOENT` if absent, `EISDIR` if a directory, `EIO` if the bytes are not valid UTF-8 (no lossy replacement). |
| `fs.writeTextFile` | `{ path, contents }` | `null` | Creates or overwrites `path`. A missing `contents` (non-string) is `EINVAL`. The parent directory must exist (`ENOENT` otherwise). Write is atomic (temp + rename). |
| `fs.exists` | `{ path }` | boolean | True iff something exists at `path`. |
| `fs.mkdir` | `{ path, recursive? }` | `null` | Creates a directory. `recursive` creates parents and is idempotent. Non-recursive on an existing path → `EEXIST`. |
| `fs.remove` | `{ path, recursive? }` | `null` | Removes a file or directory. `ENOENT` if absent. A non-empty directory without `recursive` → `ENOTEMPTY`. |
| `fs.readDir` | `{ path }` | `[{ name, isDirectory }]` | Directory entries (order unspecified). `ENOTDIR` if `path` is not a directory. |
| `fs.stat` | `{ path }` | `{ type, size, modified }` | `type` is `"file"` or `"directory"`; `size` in bytes; `modified` in epoch milliseconds. |
| `fs.openWrite` | `{ path }` | `{ url }` | Binary write (contract §5a). Returns an `app://io/<token>` URL; JS `fetch(url, { method: "PUT", body })` streams the bytes, the PUT response carries the result/error frame. Parent must exist (`ENOENT`). |
| `fs.openRead` | `{ path }` | `{ url }` | Binary read (§5a). Returns an `app://io/<token>` URL; JS `fetch(url)` streams the body. `ENOENT`/`EISDIR` as `readTextFile`. |

Binary (`openRead`/`openWrite`) never travels the message channel — it rides the `app://io`
scheme handler (contract §5a). Tokens are single-use and expire after 30 s idle.

Normative:

- `readTextFile`/`writeTextFile` are UTF-8. Content round-trips byte-for-byte, unicode included.
- `writeTextFile` does not create parent directories — that is `mkdir`'s job; this keeps a typo'd path from silently scattering directories.
- Entry order from `readDir` is unspecified; callers must not depend on it.

Errors: `EINVAL`, `ENOENT`, `EEXIST`, `EISDIR`, `ENOTDIR`, `ENOTEMPTY`, `EIO`, `ENOSYS`.
