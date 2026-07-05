#include "Dispatcher.hpp"

void Dispatcher::registerAdapter(std::unique_ptr<IAdapter> adapter, IEventEmitter* emitter) {
    std::string ns = adapter->ns();
    adapter->attach(emitter);
    if (!byNs_.count(ns)) order_.push_back(ns);
    byNs_[ns] = std::move(adapter);
}

DispatchResult Dispatcher::dispatch(const std::string& cmd, const Json& args) {
    auto dot = cmd.find('.');
    std::string ns = dot == std::string::npos ? cmd : cmd.substr(0, dot);
    std::string command = dot == std::string::npos ? "" : cmd.substr(dot + 1);

    auto it = byNs_.find(ns);
    if (it == byNs_.end())
        return {false, Json(nullptr), "ENOTSUP", "namespace not served: " + ns};

    try {
        Json value = it->second->handle(command, args);
        return {true, value, "", ""};
    } catch (const EngawaError& e) {
        return {false, Json(nullptr), e.code, e.message};
    } catch (const std::exception& e) {
        // A leaked platform error MUST NOT cross the wire as-is (spec/errors.md); default to EIO.
        return {false, Json(nullptr), "EIO", e.what()};
    }
}
