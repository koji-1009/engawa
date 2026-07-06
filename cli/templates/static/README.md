# {{name}} — a static shell

A plain HTML/CSS/JS Engawa app with **no adapters** and no host API calls beyond reading the two
synchronous runtime properties (`engawa.platform`, `engawa.contractVersion`) shown in the footer.
Start here when you want a blank canvas.

## What it shows

- The minimum viable Engawa app: `engawa.json` with an empty `adapters` array, plus an `app/`
  folder served under `app://`.
- Separate `app/style.css` and `app/main.js` linked from `index.html` — the default CSP
  (`default-src app:; script-src app:`) forbids inline `<script>` and inline styles, so keep
  everything in external files (§7.3).
- `engawa.platform` / `engawa.contractVersion`, the only host surface this template touches.

## Next steps

- `engawa dev` — build (debug) and launch.
- `engawa build` — bundle a runnable app.
- To store data, look at the `minimal` template (built-in `fs`) or the `sqlite` template (a real
  database via the sqlite adapter).
