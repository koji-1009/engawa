# {{name}} — notes backed by sqlite

A notes app that stores records in a real SQLite database via the `sqlite` adapter. Notes live in
`path.appData/notes.db` and survive restarts.

## What it shows

- Declaring an adapter in `engawa.json` (`sqlite` → `EngawaSQLite` / `SqliteAdapter()`) and the
  statically-composed adapter model (contract §3).
- The `sqlite` namespace: `sqlite.open`, `sqlite.execute` (with bound `params`), `sqlite.query`,
  `sqlite.close`.
- External JS only — inline `<script>` is dead under the default CSP (§7.3).

## The adapter path (dev-time, absolute)

`engawa.json`'s `adapters[0].path` was filled in by `engawa new` with an **absolute** path into
this machine's Engawa repo (its `adapters/sqlite`). That is a developer convenience: it lets the
CLI locate and compile the reference adapter without publishing it.

A published app would instead pin the adapter by git URL + commit:

```json
{
  "package": "sqlite",
  "product": "EngawaSQLite",
  "register": "SqliteAdapter()",
  "url": "https://example.com/engawa-sqlite.git",
  "revision": "<commit-hash>"
}
```

If you move this project to another machine, re-point `path` (or switch to `url` + `revision`).

## Next steps

- `engawa dev` — build (debug) and launch.
- `engawa build` — bundle a runnable app.
