import Foundation

// JSON helpers shared across the host. The wire is JSON strings (contract §2);
// internally we carry frames as Foundation `Any` graphs and (de)serialize at the edges.

enum JSON {
    /// Serialize an `Any` graph to a compact JSON string (scalars allowed at top level).
    static func string(_ value: Any) -> String? {
        guard let data = try? JSONSerialization.data(withJSONObject: value, options: [.fragmentsAllowed]),
              let s = String(data: data, encoding: .utf8) else { return nil }
        return s
    }

    /// Parse a JSON string into an `Any` graph.
    static func parse(_ text: String) -> Any? {
        guard let data = text.data(using: .utf8) else { return nil }
        return try? JSONSerialization.jsonObject(with: data, options: [.fragmentsAllowed])
    }

    /// Produce a JS string literal for the given string, safe to embed in evaluateJavaScript.
    /// JSON string encoding handles quotes/backslashes/control chars; U+2028/U+2029 are legal
    /// in JSON but illegal in JS string literals, so escape them explicitly.
    static func jsStringLiteral(_ s: String) -> String {
        let lit = string(s) ?? "\"\""
        return lit
            .replacingOccurrences(of: "\u{2028}", with: "\\u2028")
            .replacingOccurrences(of: "\u{2029}", with: "\\u2029")
    }
}

// Line-oriented stdout, JSON per line, for the conformance control protocol.
// Diagnostics go to stderr so they never corrupt the stdout channel.
enum Out {
    private static let lock = NSLock()

    static func line(_ obj: [String: Any]) {
        guard let s = JSON.string(obj) else { return }
        lock.lock(); defer { lock.unlock() }
        FileHandle.standardOutput.write((s + "\n").data(using: .utf8)!)
    }

    static func err(_ message: String) {
        FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
    }
}
