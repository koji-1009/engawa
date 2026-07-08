import Foundation
import UserNotifications
import EngawaKit

// The `notification` namespace (spec/commands/notification.md). Under conformance it records
// requests and exposes them via `notification.__recorded`. In app mode it delivers for real via
// UNUserNotificationCenter (best-effort, matching the Windows/Linux hosts) — but only inside a
// bundled, code-signed .app (a bare host has no bundle identifier and logs instead of trapping).
final class NotificationAdapter: Adapter, @unchecked Sendable {
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
                record(title: title, body: body)
            } else {
                deliver(title: title, body: body)
            }
            return .null

        case "__recorded" where conformance:
            return snapshot()

        default:
            throw AdapterError("ENOSYS", "unknown command: notification.\(cmd)")
        }
    }

    // Best-effort real delivery via UNUserNotificationCenter (matches the Windows/Linux hosts).
    // UNUserNotificationCenter.current() traps outside a bundled, code-signed .app, so guard on the
    // bundle identifier — a bare host (dev/`ENGAWA_APP_ROOT` runs) logs instead of crashing. Delivery
    // is best-effort: a denied authorization is dropped silently, never surfaced as an error.
    private func deliver(title: String, body: String) {
        guard Bundle.main.bundleIdentifier != nil else {
            Out.err("notification.show: \(title) — \(body)  (no .app bundle; real delivery needs a packaged app)")
            return
        }
        // Fetch the center fresh inside the closure rather than capturing it — it is non-Sendable and
        // the authorization completion is a @Sendable closure. `title`/`body` are Sendable Strings.
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound]) { granted, _ in
            guard granted else { return }
            let content = UNMutableNotificationContent()
            content.title = title
            content.body = body
            UNUserNotificationCenter.current().add(UNNotificationRequest(identifier: UUID().uuidString, content: content, trigger: nil))
        }
    }

    private func record(title: String, body: String) {
        lock.lock(); defer { lock.unlock() }
        recorded.append(.object(["title": .string(title), "body": .string(body)]))
    }

    private func snapshot() -> JSONValue {
        lock.lock(); defer { lock.unlock() }
        return .array(recorded)
    }
}
