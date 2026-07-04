# Engawa Asset Resolution (`app://`)

**Status: DRAFT.** Normative once folded into `contract-1.0`. Governs how a host serves the app's assets over the `app://` scheme (contract §5). Grows with implementation.

Keywords MUST / MUST NOT / SHOULD follow RFC 2119.

## Scheme

- All platforms serve app assets over the custom `app://` scheme. A host MUST NOT use `http://localhost` (contract §5).
- `app://io/*` is reserved for the binary I/O channel (contract §5a). Asset serving MUST NOT collide with it.

## Root resolution

_Settled in bootstrap stage 2, alongside the vertical slice and the `app://io` PUT-body spike (design.md "Known risks")._

## 404 behavior

_Bootstrap stage 2._

## MIME types

A host MUST serve correct MIME types (contract §5). Extension→type mapping table: _bootstrap stage 2._
