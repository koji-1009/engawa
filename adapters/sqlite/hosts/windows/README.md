# adapters/sqlite/hosts/windows — Windows native impl

The `sqlite` namespace (adapters/sqlite/spec.md) for the Windows host, over the SQLite C API (the
`sqlite3` amalgamation vendored under `hosts/windows/third_party/`). Request-driven, local, durable —
data here survives independently of WebView-managed storage (contract §10). Booleans bind as 0/1;
blobs are out of the v1 text scope (§5a covers binary). Each `open` is a distinct connection closed on
`close`, releasing the file handle.

Per §3 the sources live here, beside `hosts/macos/`; the Windows reference host
(`hosts/windows/CMakeLists.txt`) globs `adapters/*/hosts/windows/*.cpp` and compiles them into the one
exe (static composition, §3 — no dynamic loading).
