# `clipboard` — system clipboard (text)

| Command | Args | Returns | Notes |
|---------|------|---------|-------|
| `clipboard.writeText` | `{ text }` | `null` | Replaces the clipboard's contents with `text`. Missing `text` → `EINVAL`. |
| `clipboard.readText` | — | string | The clipboard's text, or `""` if it holds no text. |

Normative:

- Text is UTF-8 and round-trips unchanged.
- `readText` never rejects for an empty or non-text clipboard; it returns `""`.

Errors: `EINVAL`, `ENOSYS`.
