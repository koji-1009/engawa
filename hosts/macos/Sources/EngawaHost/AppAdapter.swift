import Cocoa
import EngawaKit

// The `app` namespace (spec/commands/app.md): identity, engine info, lifecycle.
struct AppAdapter: Adapter {
    let namespace = "app"
    let appVersion: String
    let hostVersion: String
    let contractVersion: String
    var autotest = false

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
            await MainActor.run { NSApp.terminate(nil) }
            return .null
        case "__exit" where autotest:
            // Autotest-only: end the process with an explicit code so the make-notes gate can
            // read pass/fail per launch. The app awaits its result-file write before calling this.
            exit(Int32(args.objectValue?["code"]?.numberValue ?? 0))
        default:
            throw AdapterError("ENOSYS", "unknown command: app.\(cmd)")
        }
    }
}
