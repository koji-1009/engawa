# adapters/sqlite/hosts/linux — Linux native impl

The `sqlite` namespace (adapters/sqlite/spec.md) for the Linux host, over the SQLite C API (the
`sqlite3` amalgamation fetched pinned + hashed by `deps.cmake` into the git-ignored build dir).
Request-driven, local, durable — data here survives independently of WebView-managed storage
(contract §10). Booleans bind as 0/1; blobs are out of the v1 text scope (§5a covers binary). Each
`open` is a distinct connection closed on `close`.

Per §3 the sources live here, beside `hosts/windows/` and `hosts/macos/`. Composition convention
(docs/design.md): `SqliteAdapter.cpp` defines the free factory `makeSqliteAdapter()`, and `deps.cmake`
declares the native dependency (the amalgamation); `engawa_add_host` (hosts/linux/engawa-host.cmake)
compiles both into the one binary (static composition, §3 — no dynamic loading). The adapter source is
byte-identical to the Windows impl (pure sqlite3 C API); only `deps.cmake`'s compiler flag differs.
