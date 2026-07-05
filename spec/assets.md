# Engawa Asset Resolution (`app://`)

**Status: DRAFT.** Normative once folded into `contract-1.0.0`. Governs how a host serves the app's assets over the `app://` scheme (contract §5). Grows with implementation.

Keywords MUST / MUST NOT / SHOULD follow RFC 2119.

## Scheme and origins

- All platforms serve app assets over the custom `app://` scheme. A host MUST NOT use `http://localhost` (contract §5).
- The app's assets are served from a single **app origin**: `app://app/`. The reference host uses the authority `app`; the authority is fixed per host build.
- `app://io/` is the reserved binary I/O origin (contract §5a) — a **distinct origin** from the app origin. Asset serving MUST NOT resolve the `io` authority, and the app origin MUST NOT serve tokens; the two never collide because they are different authorities. Because they are distinct origins, `app://io` responses carry CORS headers opting the app origin in (contract §5a).

## Root resolution

- A request `app://app/<path>` resolves to `<assetRoot>/<path>`. An empty or `/` path resolves to `/index.html`.
- The resolved path MUST stay within `<assetRoot>`; a path that escapes it (`..` traversal) is `403`. This is a structural guard, not the v1 security boundary (§7 trusts the app author).

## 404 behavior

- A request that resolves within the root but has no backing file is `404` with a `text/plain` body. This includes a bare **directory** request (e.g. `app://app/docs/`) that has no `docs/index.html`: only the app **root** (`/` or empty path) resolves to `index.html`; other directory paths are not auto-indexed and are `404`. _Richer 404 / SPA-fallback semantics are deferred._
- **Range requests** (`206 Partial Content`) are **out of scope in v1**: a host MAY answer the full body with `200`. Apps needing seekable large media should stream from `app://io` or `fs` rather than rely on ranged `app://` GETs.

## Response headers (per response class)

**App asset responses** (`app://app/…`):

- `Content-Type` (by extension, below) and `Content-Length`.
- `Content-Security-Policy` (contract §7.3): `default-src app:; script-src app:` at minimum, plus any `engawa.json` relaxations applied verbatim.

**Binary I/O responses** (`app://io/…`, contract §5a):

- `Access-Control-Allow-Origin` covering the app origin (so `fetch` from an app document can read the result), `Access-Control-Allow-Methods: GET, PUT`, and a `204` on a CORS preflight (`OPTIONS`) where the engine issues one.
- **Status is always `200`** (except the `204` preflight); success and failure are carried in the body, not the HTTP status, so the JS side reads them uniformly. A **PUT** always returns a JSON envelope `{ ok, value } | { ok:false, err:{ code, message } }` (`value:{ bytesWritten }` on success). A **GET** returns the raw file bytes as `application/octet-stream` on success, or the same JSON error envelope on failure. An invalid/expired/consumed token, or a method that doesn't match the token's direction, is `{ ok:false, err:{ code:"EINVAL" } }`; an I/O failure is `EIO`.

## MIME types

A host MUST serve correct MIME types by file extension (contract §5). Baseline mapping (a host MAY extend it):

| Extension | Content-Type |
|-----------|--------------|
| `.html` `.htm` | `text/html; charset=utf-8` |
| `.js` `.mjs` | `text/javascript; charset=utf-8` |
| `.css` | `text/css; charset=utf-8` |
| `.json` | `application/json` |
| `.png` | `image/png` |
| `.jpg` `.jpeg` | `image/jpeg` |
| `.svg` | `image/svg+xml` |
| `.wasm` | `application/wasm` |
| _other_ | `application/octet-stream` |
