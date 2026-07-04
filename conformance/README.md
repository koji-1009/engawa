# conformance

The executable form of the spec (design.md). A host is conformant when this suite passes — run against the **macOS reference host AND the Node mock host** (contract §11).

- `run.js` — runner entry point, invoked by `make conformance`.
- Suite modules and the Node mock host land in bootstrap stage 2.

Any requirement the mock host cannot satisfy without emulating a specific engine is an engine-ism smuggled into the contract — a spec bug (contract §11).
