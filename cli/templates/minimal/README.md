# {{name}} — a minimal notes app

A tiny notes app backed by the built-in `fs` namespace: it reads and writes a JSON file under
`path.appData`, so it builds and runs with **no adapters**. This is the default `engawa new`
template and the shortest path to a working, data-durable app.

## What it shows

- The `engawa.invoke` request/response flow (`path.appData`, `fs.readTextFile`, `fs.writeTextFile`).
- Durable storage (contract §10) without any adapter.
- The default-CSP discipline: all JavaScript lives in an external `app/main.js` — inline
  `<script>` is dead under `default-src app:; script-src app:` (§7.3).

## Next steps

- `engawa dev` — build (debug) and launch.
- `engawa build` — bundle a runnable app.
- Want a real database? See the `sqlite` template (`engawa new <name> --template sqlite`), which
  swaps the JSON file for the sqlite adapter.
