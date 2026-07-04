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

_The registry is seeded with the two codes the contract already names. Command namespaces add their codes here in the same commit as their spec section and conformance test (bootstrap stage 4)._
