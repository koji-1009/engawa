# adapters/sqlite — reference adapter (durable storage)

The reference adapter and one of v1's two faces: **data does not depend on the WebView's whims** (design.md). Request-driven, local. Mirrors the external adapter layout exactly (contract §3) and is extractable verbatim. Serves the `sqlite` namespace. Built in bootstrap stage 5 — layout: `spec.md`, `conformance/`, `js/`, `hosts/{macos,windows,linux}/`.
