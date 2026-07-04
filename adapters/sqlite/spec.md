# `sqlite` adapter — durable local storage

Normative spec for the `sqlite` namespace. A reference adapter (contract §3): request-driven,
local, durable. Data placed here survives independently of WebView-managed storage (contract §10)
— this is half of what makes an Engawa app an app, not a page (design.md).

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `sqlite.open` | `{ path }` | `{ db }` | Opens (creating if absent) the database at `path`; `":memory:"` for an in-memory db. `db` is an integer handle. Open failure → `ESQLITE`. |
| `sqlite.execute` | `{ db, sql, params? }` | `{ changes, lastInsertRowid }` | Runs a non-query statement (INSERT/UPDATE/DELETE/DDL). |
| `sqlite.query` | `{ db, sql, params? }` | `{ rows }` | Runs a SELECT; `rows` is an array of objects keyed by column name. |
| `sqlite.close` | `{ db }` | `null` | Closes the handle. |

- `params` is a positional array bound to `?` placeholders. Supported value types: null, boolean,
  number, string. Booleans bind as 0/1 (SQLite has no boolean type).
- Result values are null, number (INTEGER/REAL), or string (TEXT). **Blobs are out of the v1 text
  scope** — binary belongs on the `app://io` channel (contract §5a), never the message channel.
- An unknown `db` handle → `EBADF`. Malformed SQL / constraint failure → `ESQLITE`. Missing
  `path`/`sql`/`db` → `EINVAL`.

Durability: data written and committed under a file `path` is present after `close` + re-`open`
(the conformance suite asserts this).

Errors: `EINVAL`, `EBADF`, `ESQLITE`, `ENOSYS`.
