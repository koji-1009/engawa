import Foundation

// The `notification` namespace (spec/commands/notification.md). Under conformance it records
// requests and exposes them via `notification.__recorded`. Real delivery needs a bundled app
// with a notification entitlement (UNUserNotificationCenter), wired at the packaging stage;
// until then app-mode delivery is a logged best-effort placeholder.
final class NotificationAdapter: Adapter {
    let namespace = "notification"
    private let conformance: Bool
    private let lock = NSLock()
    private var recorded: [JSONValue] = []

    init(conformance: Bool) {
        self.conformance = conformance
    }

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        let obj = args.objectValue ?? [:]

        switch cmd {
        case "show":
            guard let title = obj["title"]?.stringValue, !title.isEmpty else { throw AdapterError("EINVAL", "title required") }
            let body = obj["body"]?.stringValue ?? ""
            if conformance {
                lock.lock()
                recorded.append(.object(["title": .string(title), "body": .string(body)]))
                lock.unlock()
            } else {
                Out.err("notification.show: \(title) — \(body)  (delivery wired at packaging stage)")
            }
            return .null

        case "__recorded" where conformance:
            lock.lock(); defer { lock.unlock() }
            return .array(recorded)

        default:
            throw AdapterError("ENOSYS", "unknown command: notification.\(cmd)")
        }
    }
}
