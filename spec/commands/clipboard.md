# `clipboard` — system clipboard (text)

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `clipboard.writeText` | `{ text }` | `null` | Replaces the clipboard's contents with `text`. Missing `text` → `EINVAL`. |
| `clipboard.readText` | — | string | The clipboard's text, or `""` if it holds no text. |

Normative:

- Text is UTF-8 and round-trips unchanged.
- `readText` never rejects for an empty or non-text clipboard; it returns `""`.
- A `writeText` followed by `readText` MUST return the written text (the write→read round-trip is
  guaranteed and conformance-tested). Reflecting clipboard writes made by *other* applications is
  best-effort: a host whose platform clipboard broker cannot be read without blocking the single
  command loop (notably a GTK/WebKitGTK host on a headless or broker-less display, e.g. offscreen
  conformance and `engawa dev` under WSLg) MAY back `readText` with an in-process mirror of this app's
  own last write rather than a synchronous system-clipboard read. The command MUST NOT block.

Errors: `EINVAL`, `ENOSYS`.
