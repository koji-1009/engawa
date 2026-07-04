import Foundation

// Single-use tokens for the binary I/O channel (contract §5a). fs.openRead/openWrite mint a
// token bound to a path + direction; the app:// scheme handler redeems it exactly once. Tokens
// expire after 30 s idle. Shared between the fs adapter (mint) and the scheme handler (redeem).
final class IoTokenStore: @unchecked Sendable {
    enum Mode { case read, write }
    struct Entry { let path: String; let mode: Mode; let created: Date }

    private let lock = NSLock()
    private var tokens: [String: Entry] = [:]
    private let ttl: TimeInterval = 30

    func mint(path: String, mode: Mode) -> String {
        let token = UUID().uuidString
        lock.lock(); tokens[token] = Entry(path: path, mode: mode, created: Date()); lock.unlock()
        return token
    }

    /// Redeem a token once. Returns nil if unknown, already consumed, or expired (30 s idle).
    func take(_ token: String) -> Entry? {
        lock.lock(); defer { lock.unlock() }
        guard let entry = tokens.removeValue(forKey: token) else { return nil }
        if Date().timeIntervalSince(entry.created) > ttl { return nil }
        return entry
    }
}
