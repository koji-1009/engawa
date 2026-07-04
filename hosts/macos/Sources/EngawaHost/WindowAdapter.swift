import Foundation
import EngawaKit

// The `window` namespace (spec/commands/window.md). Thin routing over WindowController;
// all AppKit work hops to the main actor. `requestClose` is a conformance-only testability
// hook that drives the same close protocol a real user close does.
struct WindowAdapter: Adapter {
    let namespace = "window"
    let controller: WindowController
    let conformance: Bool

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        let obj = args.objectValue ?? [:]

        switch cmd {
        case "setTitle":
            let title = obj["title"]?.stringValue ?? ""
            await MainActor.run { controller.setTitle(title) }
            return .null

        case "getSize":
            return await MainActor.run { controller.size() }

        case "setSize":
            guard let w = obj["width"]?.numberValue, let h = obj["height"]?.numberValue else {
                throw AdapterError("EINVAL", "width and height required")
            }
            await MainActor.run { controller.setSize(width: w, height: h) }
            return .null

        case "setResizable":
            let resizable = obj["resizable"]?.boolValue ?? true
            await MainActor.run { controller.setResizable(resizable) }
            return .null

        case "minimize":
            await MainActor.run { controller.minimize() }
            return .null

        case "maximize":
            await MainActor.run { controller.maximize() }
            return .null

        case "close":
            await MainActor.run { controller.close() }
            return .null

        case "respondToClose":
            guard let token = obj["token"]?.numberValue else {
                throw AdapterError("EINVAL", "token required")
            }
            let allow = obj["allow"]?.boolValue ?? false
            try await MainActor.run { try controller.respondToClose(token: Int(token), allow: allow) }
            return .null

        case "requestClose" where conformance:
            await MainActor.run { _ = controller.beginClose() }
            return .null

        default:
            throw AdapterError("ENOSYS", "unknown command: window.\(cmd)")
        }
    }
}
