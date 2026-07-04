import Cocoa
import EngawaKit

// The `shellOpen` namespace (spec/commands/shellOpen.md). Under conformance it records
// requests instead of performing them (the OS side effect is unobservable and unwanted in
// tests) and exposes them via `shellOpen.__recorded`.
final class ShellOpenAdapter: Adapter, @unchecked Sendable {
    let namespace = "shellOpen"
    private let conformance: Bool
    private let lock = NSLock()
    private var recorded: [JSONValue] = []

    init(conformance: Bool) {
        self.conformance = conformance
    }

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        let obj = args.objectValue ?? [:]

        switch cmd {
        case "openExternal":
            guard let url = obj["url"]?.stringValue, !url.isEmpty else { throw AdapterError("EINVAL", "url required") }
            if conformance {
                record(["action": .string("openExternal"), "url": .string(url)])
            } else if let u = URL(string: url) {
                await MainActor.run { NSWorkspace.shared.open(u) }
            }
            return .null

        case "revealInFolder":
            guard let p = obj["path"]?.stringValue, !p.isEmpty else { throw AdapterError("EINVAL", "path required") }
            if conformance {
                record(["action": .string("revealInFolder"), "path": .string(p)])
            } else {
                await MainActor.run { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: p)]) }
            }
            return .null

        case "__recorded" where conformance:
            return snapshot()

        default:
            throw AdapterError("ENOSYS", "unknown command: shellOpen.\(cmd)")
        }
    }

    private func record(_ entry: [String: JSONValue]) {
        lock.lock(); defer { lock.unlock() }
        recorded.append(.object(entry))
    }

    private func snapshot() -> JSONValue {
        lock.lock(); defer { lock.unlock() }
        return .array(recorded)
    }
}
