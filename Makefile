# Engawa — build & verification entry points.
# The two gates in CLAUDE.md "Definition of done" are `make conformance` and `make notes`.
# Nothing else here is normative; the contract lives in spec/.

.DEFAULT_GOAL := help

NODE ?= node

.PHONY: help conformance notes clean host-macos

help:
	@echo "Engawa targets:"
	@echo "  make conformance  — run the conformance suite (macOS host AND Node mock host)"
	@echo "  make notes        — run the examples/notes acceptance gate"
	@echo "  make clean        — remove build artifacts"

# Gate 1 — conformance suite. Runs against the macOS host and the Node mock host.
conformance: host-macos
	$(NODE) conformance/run.js

# Build the Swift reference host (the runner needs its binary present).
host-macos:
	cd hosts/macos && swift build

# Gate 2 — acceptance gate. Build host + sqlite/update adapters, bundle examples/notes,
# scripted write/read/quit/relaunch/read-back/signed-update/relaunch (CLAUDE.md).
notes:
	bash examples/notes/gate/gate.sh

clean:
	rm -rf build
