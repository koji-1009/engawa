import Foundation

// The host-side adapter API (contract §3). An adapter serves exactly one namespace
// over the wire protocol; it never touches IPC. Built-in namespaces (§4) are adapters
// that ship in-tree — there is no privileged dispatch path.
protocol Adapter {
    var namespace: String { get }
    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue
    func attach(_ emitter: EventEmitter)
}

extension Adapter {
    // Most adapters emit no events; opt in by overriding.
    func attach(_ emitter: EventEmitter) {}
}

// How an adapter emits an event frame (contract §2, §2.1). Signals, not bulk payloads.
protocol EventEmitter {
    func emit(_ topic: String, _ payload: JSONValue)
}

// A command failed with a wire error code (registry: spec/errors.md).
struct AdapterError: Error {
    let code: String
    let message: String
    init(_ code: String, _ message: String) {
        self.code = code
        self.message = message
    }
}

// Namespace → adapter registry. The router is the single dispatch path: a request is
// routed to the adapter that owns its namespace, built-in or not. No adapter is special.
final class Router {
    private var adapters: [String: Adapter] = [:]
    private let emitter: EventEmitter

    init(emitter: EventEmitter) {
        self.emitter = emitter
    }

    func register(_ adapter: Adapter) {
        adapters[adapter.namespace] = adapter
        adapter.attach(emitter)
    }

    func adapter(for namespace: String) -> Adapter? {
        adapters[namespace]
    }

    /// Every served namespace — the value of `capabilities` in the handshake (§1, §3).
    var namespaces: [String] {
        adapters.keys.sorted()
    }
}

// Bridges an adapter's emit() back to the host's outbound queue.
final class HostEmitter: EventEmitter {
    private let sink: (String, JSONValue) -> Void
    init(_ sink: @escaping (String, JSONValue) -> Void) {
        self.sink = sink
    }
    func emit(_ topic: String, _ payload: JSONValue) {
        sink(topic, payload)
    }
}
