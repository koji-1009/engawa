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
            await controller.setTitle(title)
            return .null

        case "getSize":
            return await controller.size()

        case "setSize":
            guard let w = obj["width"]?.numberValue, let h = obj["height"]?.numberValue else {
                throw AdapterError("EINVAL", "width and height required")
            }
            await controller.setSize(width: w, height: h)
            return .null

        case "setResizable":
            let resizable = obj["resizable"]?.boolValue ?? true
            await controller.setResizable(resizable)
            return .null

        case "minimize":
            await controller.minimize()
            return .null

        case "maximize":
            await controller.maximize()
            return .null

        case "close":
            await controller.close()
            return .null

        case "setCloseHandler":
            let enabled = obj["enabled"]?.boolValue ?? false
            await controller.setCloseHandler(enabled)
            return .null

        case "respondToClose":
            guard let token = obj["token"]?.numberValue else {
                throw AdapterError("EINVAL", "token required")
            }
            let allow = obj["allow"]?.boolValue ?? false
            try await controller.respondToClose(token: Int(token), allow: allow)
            return .null

        case "requestClose" where conformance:
            let deferred = await controller.requestCloseForTest()
            return .object(["deferred": .bool(deferred)])

        case "__lastCloseAllowed" where conformance:   // the last respondToClose allow decision
            return await controller.lastCloseAllowed.map { JSONValue.bool($0) } ?? .null

        case "__resizeStorm" where conformance:   // fire many resizes in one tick (§2.1 coalescing)
            let count = Int(obj["count"]?.numberValue ?? 8)
            let from = obj["from"]?.numberValue ?? 300
            await controller.resizeStorm(from: from, count: count)
            return .object(["from": .number(from), "count": .number(Double(count)), "last": .number(from + Double(count - 1))])

        default:
            throw AdapterError("ENOSYS", "unknown command: window.\(cmd)")
        }
    }
}
