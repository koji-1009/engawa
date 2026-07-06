#pragma once
// Routes a wire request to the adapter serving its namespace and maps failures to registry codes
// (spec/errors.md). No privileged path for built-ins — window/fs/… are adapters like any other (§3).
// shell.js rejects unserved namespaces locally with ENOTSUP (§1.1), so a request reaching here has a
// served namespace; an unknown *command* within it is ENOSYS. A namespace that only a direct
// control-channel invoke could reach unserved still answers ENOTSUP, identically to shell.js.
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Adapter.hpp"

struct DispatchResult {
    bool ok = false;
    Json value;
    std::string code;
    std::string message;
};

class Dispatcher {
public:
    void registerAdapter(std::unique_ptr<IAdapter> adapter, IEventEmitter* emitter);

    // The served namespaces — this becomes __shell.capabilities (§1, §3). Order is insignificant.
    const std::vector<std::string>& capabilities() const { return order_; }

    DispatchResult dispatch(const std::string& cmd, const Json& args);

private:
    std::vector<std::string> order_;
    std::unordered_map<std::string, std::unique_ptr<IAdapter>> byNs_;
};
