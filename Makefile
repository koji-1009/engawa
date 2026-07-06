# Engawa — build & verification entry points.
# The two gates in CLAUDE.md "Definition of done" are `make conformance` and `make notes`.
# Nothing else here is normative; the contract lives in spec/.

.DEFAULT_GOAL := help

NODE ?= node

# The native reference host + gate are the current platform's. The suite/gate INTENT is unchanged
# (native host + mock; scripted write/read/update); only which native host runs is per-OS. On macOS
# this resolves exactly as before.
ifeq ($(OS),Windows_NT)
  UNAME := Windows
else
  UNAME := $(shell uname -s)
endif

ifeq ($(UNAME),Windows)
  NATIVE_HOST := host-windows
  # Windows PowerShell 5.1 (`powershell`) ships on every Windows; gate.ps1 uses nothing PS7-only.
  # -ExecutionPolicy Bypass avoids a policy block on running the script file.
  NOTES_GATE := powershell -NoProfile -ExecutionPolicy Bypass -File examples/notes/gate/gate.ps1
else ifeq ($(UNAME),Darwin)
  NATIVE_HOST := host-macos
  NOTES_GATE := bash examples/notes/gate/gate.sh
else
  NATIVE_HOST := host-linux
  NOTES_GATE := bash examples/notes/gate/gate.sh
endif

.PHONY: help conformance notes clean host-macos host-windows host-windows-smoke host-linux cli-test

help:
	@echo "Engawa targets:"
	@echo "  make conformance  — run the conformance suite (native host AND Node mock host)"
	@echo "  make notes        — run the examples/notes acceptance gate"
	@echo "  make clean        — remove build artifacts"

# Gate 1 — conformance suite. Runs against the current platform's native host and the mock host.
conformance: $(NATIVE_HOST)
	$(NODE) conformance/run.js

# Build the Swift reference host (the runner needs its binary present).
host-macos:
	cd hosts/macos && swift build

# Build the Windows reference host (C++ + WebView2). Needs the VS C++ workload + WebView2 runtime.
host-windows:
	powershell -NoProfile -ExecutionPolicy Bypass -File hosts/windows/build.ps1

# Native-path smoke test (Windows): fires a real toast + instantiates the file dialog COM object —
# the dialog/notification paths the conformance suite covers only via substitutes. Fires a visible
# toast, so it is a developer/interactive-session check, not part of `make conformance`.
host-windows-smoke:
	powershell -NoProfile -ExecutionPolicy Bypass -File hosts/windows/test/smoke.ps1

# Build the Linux reference host (C++ + GTK3 + WebKitGTK). Needs g++, cmake, ninja and the -dev
# packages for gtk+-3.0, webkit2gtk-4.1, libsoup-3.0, libsodium.
host-linux:
	bash hosts/linux/build.sh

# Gate 2 — acceptance gate. Build host + sqlite/update adapters, bundle examples/notes,
# scripted write/read/quit/relaunch/read-back/signed-update/relaunch (CLAUDE.md).
notes:
	$(NOTES_GATE)

# CLI unit tests — host composition + §7.1 trust-root baking. Pure Node, no native toolchain,
# cross-platform. Not one of the two gates; run alongside them.
cli-test:
	cd cli && $(NODE) --test "test/**/*.test.ts"

clean:
	rm -rf build
