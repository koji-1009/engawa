# examples/notes — acceptance-gate app

A minimal notes app over SQLite, update-enabled — the app `make notes` drives (CLAUDE.md, design.md "Two verification layers"). In autotest mode (`ENGAWA_AUTOTEST=1`) it drives itself — write/read/update — and reports via exit code + a result file; no UI scripting. Built in bootstrap stage 6.
