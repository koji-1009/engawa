#pragma once
// App-adapter composition seam (§3 static composition). The core registers the built-ins (§4) and
// the contract-coupled `update`; this hook registers the app's DECLARED adapters. The default in-tree
// build provides Compose.cpp (registers the reference `sqlite`, so the conformance host has it);
// `engawa dev/build` compiles a generated Compose in its place that registers exactly the adapters
// the app's engawa.json declared (docs/design.md "Composition"; spec/commands/README.md).
#include <string>

#include "Adapter.hpp"
#include "Dispatcher.hpp"

void registerAppAdapters(Dispatcher& dispatcher, IEventEmitter* emitter);

// The update trust root (§7.1), compiled into the host at build time. The default in-tree build
// returns empty — the reference host takes its trust root from the environment (conformance suite,
// make-notes gate, `engawa dev`). A distributable built by `engawa build` provides a generated
// Compose whose bakedTrustRoot() returns the app publisher's ed25519 public key (base64), so the
// key cannot be swapped by editing a file beside the executable.
std::string bakedTrustRoot();
