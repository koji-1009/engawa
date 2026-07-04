# Engawa Error Registry

**Status: DRAFT.** Grows with implementation (contract §2). Each command enumerates its error codes in `spec/commands/<namespace>.md`; this file is the flat registry every code maps into, so the same condition uses the same code across namespaces.

Keywords MUST / MUST NOT / SHOULD follow RFC 2119.

## Rules

- A host MUST map platform-native errors to a code in this registry. A raw platform error MUST NOT cross the wire.
- Error frames carry `{ code, message }` (contract §2). `code` is the contract; `message` is human-readable and non-normative.
- Codes are stable strings. Adding a code is additive. Changing a code's meaning is a breaking change.

## Registry

| Code | Meaning |
|------|---------|
| `ENOENT` | A referenced path does not exist (contract §2 example). |
| `EINVAL` | An argument is malformed or out of range; for `window.respondToClose`, an unknown or already-consumed token (contract §4.2). |
| `ENOTSUP` | The command's namespace is not in `capabilities`. shell.js rejects locally, without a host round-trip (contract §1.1). |
| `ENOSYS` | The namespace is served but has no such command. |
| `EIO` | An underlying OS operation failed (create, read, write) for a reason with no more specific code. |
| `EEXIST` | The target already exists and the operation requires it not to (`fs.mkdir` non-recursive). |
| `EISDIR` | The target is a directory where a file was required (`fs.readTextFile`). |
| `ENOTDIR` | The target is not a directory where one was required (`fs.readDir`). |
| `ENOTEMPTY` | A directory is not empty and the operation needs it to be (`fs.remove` without `recursive`). |
| `EPERM` | The operation is not permitted: `process.spawn` of an undeclared or out-of-bundle target (contract §7.2). |
| `ESRCH` | No process with the given `pid` (`process.*`). |
| `EBADF` | An unknown or closed handle (e.g. a `sqlite` db handle). |
| `ESQLITE` | A SQLite error (open failed, malformed SQL, constraint violation, …). Adapter code from `adapters/sqlite`. |
| `ESIGNATURE` | An update payload's signature did not verify against the embedded trust root (contract §7.1). The payload is discarded; there is no override. |
| `EHASH` | An update payload's content hash did not match the manifest (contract §8). |

_Each command namespace and adapter registers its codes here in the same commit as its spec section and conformance test (CLAUDE.md commit discipline)._
