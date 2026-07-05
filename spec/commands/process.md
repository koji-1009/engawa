# `process` — sidecar processes

App-specific native work runs as a spawned sidecar over stdio (design.md "Sidecars below
adapters"). Streams are **pull**, never events with bulk payloads (contract §2.1, §4.1).

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `process.spawn` | `{ command, args? }` | `{ pid }` | Launches a declared sidecar (§7.2). `command` must be a manifest `sidecars` entry; anything else → `EPERM`. |
| `process.stdinWrite` | `{ pid, data }` | `null` | Writes `data` (UTF-8) to the sidecar's stdin. Unknown `pid` → `ESRCH`. |
| `process.stdinClose` | `{ pid }` | `null` | Closes the sidecar's stdin, so a process that reads to EOF can finish. Unknown `pid` → `ESRCH`. |
| `process.read` | `{ pid, stream, maxBytes }` | `{ data, eof }` | Drains up to `maxBytes` bytes from `stream` (`"stdout"`/`"stderr"`), decoded as UTF-8 without splitting a multibyte sequence. `eof` is true once the process has exited and the stream is fully drained. `stream` defaults to `"stdout"`; a `stream` other than `"stdout"`/`"stderr"`, or a non-number/negative `maxBytes`, → `EINVAL`. **Forward progress:** if `maxBytes` is smaller than the next fully-buffered character, the host returns that whole character anyway (so a tiny `maxBytes` never livelocks); `data:""` with `eof:false` means only an incomplete multibyte sequence is buffered so far — the app waits for the next `process.readable`. Unknown `pid` → `ESRCH`. |
| `process.kill` | `{ pid }` | `null` | Terminates the sidecar (SIGTERM). Unknown `pid` → `ESRCH`. |

After `process.exit` has fired, the `pid` is drained and finished: `read` returns `{ data:"", eof:true }`, and `kill`/`stdinWrite`/`stdinClose` on it are accepted as no-ops (they do not error, since the process legitimately existed). A `pid` that never existed is always `ESRCH`.

Events:

- `process.readable` — payload `{ pid, stream }`. Signals that a drained stream has data again
  (level-triggered: emitted when the stream buffer goes non-empty; coalesced per `(pid, stream)`).
  Per §4.1 "no payload" means **no data bytes** ride the event — the `{ pid, stream }` identifiers
  are the signal, so JS knows which stream to `read`. The bytes come only from `process.read`.
- `process.exit` — payload `{ pid, code }`. Fires **only after both streams are drained** (§4.1),
  so no output is lost to a race with exit.

## Backpressure (contract §4.1)

The host buffers up to 8 MiB per stream, then stops reading the pipe (OS backpressure) until
`process.read` drains it below the cap. Text only over this channel — binary never rides the
message channel (a sidecar needing binary uses files / `app://io`).

## Sidecar allowlist (contract §7.2)

`process.spawn` runs only executables listed in the app manifest (`engawa.json`, `sidecars`) and
resolved inside the app bundle. An undeclared command, or one whose resolved path escapes the
bundle, is `EPERM`. There is no arbitrary-path execution. The allowlist is matched against the
`command` string **as declared** — the platform resolution below happens only *after* a command
has cleared the allowlist, so it can never widen it.

## Platform executable resolution (normative)

A `sidecars` entry names one logical executable; its on-disk image differs per OS. A host resolves
a declared, in-bundle `command` to a runnable image using the host platform's conventions:

- A host MUST first try the declared path verbatim, **if the OS can execute it**. On POSIX that is
  any file with an executable bit; on Windows only a file whose extension is a platform executable
  extension (`.exe`, `.com`, `.bat`, `.cmd`) — a bare, extension-less path is not runnable there even
  if a file of that exact name exists.
- Where the OS distinguishes executables by extension (Windows), a host MUST then try the declared
  path with each platform executable extension appended, in that order, and run the first that
  exists. A `.bat`/`.cmd` image is launched through the platform command interpreter with the same
  stdio pipes; its exit code is the sidecar's exit code.
- The resolved image MUST still lie inside the app bundle (the §7.2 containment guard applies to
  the resolved path, not just the declared one).

This keeps one `sidecars` declaration cross-platform: the manifest names `./bin/tool`, and each
host build ships `tool` (POSIX) or `tool.exe`/`tool.cmd` (Windows) beside it.

Errors: `EINVAL`, `EPERM`, `ESRCH`, `EIO`, `ENOSYS`.
