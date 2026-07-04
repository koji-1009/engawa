import Cocoa

// The `app` namespace (spec/commands/app.md): identity, engine info, lifecycle.
struct AppAdapter: Adapter {
    let namespace = "app"
    let appVersion: String
    let hostVersion: String
    let contractVersion: String

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        switch cmd {
        case "version":
            return .string(appVersion)
        case "engineInfo":
            return .object([
                "engine": .string("WKWebView"),
                "engineVersion": .string(ProcessInfo.processInfo.operatingSystemVersionString),
                "hostVersion": .string(hostVersion),
                "contractVersion": .string(contractVersion),
            ])
        case "quit":
            DispatchQueue.main.async { NSApp.terminate(nil) }
            return .null
        default:
            throw AdapterError("ENOSYS", "unknown command: app.\(cmd)")
        }
    }
}
