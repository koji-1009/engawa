#pragma once
// App-adapter composition seam (§3 static composition). The core registers the built-ins (§4) and
// the contract-coupled `update`; this hook registers the app's DECLARED adapters. The default in-tree
// build provides Compose.cpp (registers the reference `sqlite`, so the conformance host has it);
// `engawa dev/build` compiles a generated Compose in its place that registers exactly the adapters
// the app's engawa.json declared (docs/design.md "Composition"; spec/commands/README.md).
#include "Adapter.hpp"
#include "Dispatcher.hpp"

void registerAppAdapters(Dispatcher& dispatcher, IEventEmitter* emitter);
