import Foundation

// The host-side adapter API (contract §3). An adapter serves exactly one namespace
// over the wire protocol; it never touches IPC. Built-in namespaces (§4) are adapters
// that ship in-tree — there is no privileged dispatch path.
public protocol Adapter: Sendable {
    var namespace: String { get }
    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue
    func attach(_ emitter: EventEmitter)
}

public extension Adapter {
    // Most adapters emit no events; opt in by overriding.
    func attach(_ emitter: EventEmitter) {}
}

// How an adapter emits an event frame (contract §2, §2.1). Signals, not bulk payloads.
public protocol EventEmitter: Sendable {
    func emit(_ topic: String, _ payload: JSONValue)
}

// A command failed with a wire error code (registry: spec/errors.md).
public struct AdapterError: Error, Sendable {
    public let code: String
    public let message: String
    public init(_ code: String, _ message: String) {
        self.code = code
        self.message = message
    }
}

// Namespace → adapter registry. The router is the single dispatch path: a request is
// routed to the adapter that owns its namespace, built-in or not. No adapter is special.
public final class Router {
    private var adapters: [String: Adapter] = [:]
    private let emitter: EventEmitter

    public init(emitter: EventEmitter) {
        self.emitter = emitter
    }

    public func register(_ adapter: Adapter) {
        adapters[adapter.namespace] = adapter
        adapter.attach(emitter)
    }

    public func adapter(for namespace: String) -> Adapter? {
        adapters[namespace]
    }

    /// Every served namespace — the value of `capabilities` in the handshake (§1, §3).
    public var namespaces: [String] {
        adapters.keys.sorted()
    }
}

// Bridges an adapter's emit() back to the host's outbound queue.
public final class HostEmitter: EventEmitter {
    private let sink: @Sendable (String, JSONValue) -> Void
    public init(_ sink: @escaping @Sendable (String, JSONValue) -> Void) {
        self.sink = sink
    }
    public func emit(_ topic: String, _ payload: JSONValue) {
        sink(topic, payload)
    }
}
