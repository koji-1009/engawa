# Engawa — build & verification entry points.
# The two gates in CLAUDE.md "Definition of done" are `make conformance` and `make notes`.
# Nothing else here is normative; the contract lives in spec/.

.DEFAULT_GOAL := help

NODE ?= node

.PHONY: help conformance notes clean

help:
	@echo "Engawa targets:"
	@echo "  make conformance  — run the conformance suite (macOS host AND Node mock host)"
	@echo "  make notes        — run the examples/notes acceptance gate"
	@echo "  make clean        — remove build artifacts"

# Gate 1 — conformance suite. Runs against the macOS host and the Node mock host.
conformance:
	$(NODE) conformance/run.js

# Gate 2 — acceptance gate. Build host + sqlite/update adapters, bundle examples/notes,
# scripted write/read/quit/relaunch/read-back/signed-update/relaunch (CLAUDE.md).
notes:
	@echo "make notes: acceptance gate not yet implemented (bootstrap stage 6)." >&2
	@exit 1

clean:
	rm -rf build
