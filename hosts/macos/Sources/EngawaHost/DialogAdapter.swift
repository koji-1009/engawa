import Cocoa
import EngawaKit

// The `dialog` namespace (spec/commands/dialog.md). Under conformance the host presents no UI
// and returns a preprogrammed response (dialog.__setResponse); in app mode it drives real
// AppKit panels on the main actor.
final class DialogAdapter: Adapter, @unchecked Sendable {
    let namespace = "dialog"
    private let conformance: Bool
    private let lock = NSLock()
    private var nextResponse: JSONValue?

    init(conformance: Bool) {
        self.conformance = conformance
    }

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        let obj = args.objectValue ?? [:]

        switch cmd {
        case "open":
            if conformance { return takeResponse(default: .object(["canceled": .bool(true), "paths": .array([])])) }
            return await runOpen(obj)

        case "save":
            if conformance { return takeResponse(default: .object(["canceled": .bool(true), "path": .null])) }
            return await runSave(obj)

        case "message":
            guard obj["message"]?.stringValue != nil else { throw AdapterError("EINVAL", "message required") }
            if conformance { return takeResponse(default: .object(["button": .number(0)])) }
            return await runMessage(obj)

        case "__setResponse" where conformance:
            setResponse(args)
            return .null

        default:
            throw AdapterError("ENOSYS", "unknown command: dialog.\(cmd)")
        }
    }

    private func setResponse(_ response: JSONValue) {
        lock.lock(); defer { lock.unlock() }
        nextResponse = response
    }

    private func takeResponse(default fallback: JSONValue) -> JSONValue {
        lock.lock(); defer { lock.unlock() }
        let r = nextResponse ?? fallback
        nextResponse = nil
        return r
    }

    // MARK: app-mode panels (exercised by make notes, not the headless suite)

    private func runOpen(_ obj: [String: JSONValue]) async -> JSONValue {
        await MainActor.run {
            let panel = NSOpenPanel()
            panel.canChooseFiles = !(obj["directory"]?.boolValue ?? false)
            panel.canChooseDirectories = obj["directory"]?.boolValue ?? false
            panel.allowsMultipleSelection = obj["multiple"]?.boolValue ?? false
            if let title = obj["title"]?.stringValue { panel.message = title }
            let ok = panel.runModal() == .OK
            let paths = ok ? panel.urls.map { JSONValue.string($0.path) } : []
            return .object(["canceled": .bool(!ok), "paths": .array(paths)])
        }
    }

    private func runSave(_ obj: [String: JSONValue]) async -> JSONValue {
        await MainActor.run {
            let panel = NSSavePanel()
            if let name = obj["defaultName"]?.stringValue { panel.nameFieldStringValue = name }
            if let title = obj["title"]?.stringValue { panel.message = title }
            let ok = panel.runModal() == .OK
            return .object(["canceled": .bool(!ok), "path": ok ? .string(panel.url?.path ?? "") : .null])
        }
    }

    private func runMessage(_ obj: [String: JSONValue]) async -> JSONValue {
        await MainActor.run {
            let alert = NSAlert()
            alert.messageText = obj["message"]?.stringValue ?? ""
            if let title = obj["title"]?.stringValue { alert.window.title = title }
            let buttons = obj["buttons"]?.arrayValue?.compactMap { $0.stringValue } ?? ["OK"]
            buttons.forEach { alert.addButton(withTitle: $0) }
            let response = alert.runModal()
            let index = response.rawValue - NSApplication.ModalResponse.alertFirstButtonReturn.rawValue
            return .object(["button": .number(Double(index))])
        }
    }
}
