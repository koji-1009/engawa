import Foundation

// The vertical-slice fixture, now an in-tree adapter like any other (bootstrap stage 3).
// It echoes its arguments, proving the router carries a real namespace with no privileged
// path. Stage 4 replaces it with the §4 built-in namespaces.
struct EchoAdapter: Adapter {
    let namespace = "echo"

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        return args
    }
}
