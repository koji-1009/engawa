# Third-party dependencies

Fetched at CMake configure time (see `CMakeLists.txt`), not vendored into the repo: each is
downloaded once into the git-ignored build tree (`build/_deps/`) and verified by SHA-256, so the
build is deterministic — pinned version + hash → identical bytes — without carrying ~11 MB of
third-party code in git history. All are compiled/linked statically into `EngawaHost.exe`; none ship
as a separate file beside the binary, so the host stays a single native exe with no end-user runtime
prerequisite (docs/design.md). Build-time network access is required on the first configure (a normal
CI-server cost); later configures reuse the cached, hash-verified files.

| Component | Version | Purpose | License | SHA-256 |
|-----------|---------|---------|---------|---------|
| `nlohmann/json.hpp` | 3.11.3 | Wire protocol + arg parsing (§2) | MIT | `9bea4c80…f103ea6` |
| `sqlite-amalgamation` (zip) | 3.46.1 | `sqlite` reference adapter engine | Public domain | `77823cb1…dbbe5784` |
| `tweetnacl.c` | 20140427 | ed25519 verification, update trust root (§7.1) | Public domain | `02e65bc3…a17175c4` |
| `tweetnacl.h` | 20140427 | (header for the above) | Public domain | `43f29ad7…31244287` |

Sources:
- `https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp`
- `https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip`
- `https://tweetnacl.cr.yp.to/20140427/tweetnacl.{c,h}`

SHA-256 (update payload hashing, §7.1) uses the Windows CNG API (`bcrypt`), a system component — not
fetched. WebView2's C++ SDK (headers + static loader) is restored from NuGet at configure time.
