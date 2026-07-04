# Engawa Asset Resolution (`app://`)

**Status: DRAFT.** Normative once folded into `contract-1.0`. Governs how a host serves the app's assets over the `app://` scheme (contract §5). Grows with implementation.

Keywords MUST / MUST NOT / SHOULD follow RFC 2119.

## Scheme and origins

- All platforms serve app assets over the custom `app://` scheme. A host MUST NOT use `http://localhost` (contract §5).
- The app's assets are served from a single **app origin**: `app://app/`. The reference host uses the authority `app`; the authority is fixed per host build.
- `app://io/` is the reserved binary I/O origin (contract §5a) — a **distinct origin** from the app origin. Asset serving MUST NOT resolve the `io` authority, and the app origin MUST NOT serve tokens; the two never collide because they are different authorities. Because they are distinct origins, `app://io` responses carry CORS headers opting the app origin in (contract §5a).

## Root resolution

- A request `app://app/<path>` resolves to `<assetRoot>/<path>`. An empty or `/` path resolves to `/index.html`.
- The resolved path MUST stay within `<assetRoot>`; a path that escapes it (`..` traversal) is `403`. This is a structural guard, not the v1 security boundary (§7 trusts the app author).

## 404 behavior

- A request that resolves within the root but has no backing file is `404` with a `text/plain` body. _Richer 404 semantics (SPA fallback opt-in) are deferred past the slice._

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
