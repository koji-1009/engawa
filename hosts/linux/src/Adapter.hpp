#pragma once
// An adapter serves exactly one namespace over the wire protocol (contract §3). Built-in namespaces
// (§4) are adapters that ship in-tree; there is no privileged dispatch path. Handlers are
// synchronous and throw EngawaError on failure (the reference host's HandleAsync bodies are all
// synchronous Task.FromResult, so nothing is lost).
#include <string>

#include "Json.hpp"

// How an adapter raises an event (contract §2 `evt` frames). `payload` carries signals, not bulk
// data (§2.1, §4.1). Thread-safe: a sidecar reader thread may emit process.readable off the UI thread.
struct IEventEmitter {
    virtual void emit(const std::string& topic, const Json& payload) = 0;
    virtual ~IEventEmitter() = default;
};

struct IAdapter {
    virtual std::string ns() const = 0;
    // Return the result value (may be null Json). Throw EngawaError to fail with a registry code.
    virtual Json handle(const std::string& command, const Json& args) = 0;
    // Called once at registration so the adapter can raise events.
    virtual void attach(IEventEmitter*) {}
    virtual ~IAdapter() = default;
};
