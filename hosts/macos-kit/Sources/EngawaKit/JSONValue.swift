import Foundation

// The typed JSON value that crosses the adapter boundary (contract §3).
// Numbers are IEEE doubles — the same representation JS uses — so a value that
// round-trips through an adapter is bit-identical to what the page sent.
public indirect enum JSONValue: Sendable {
    case null
    case bool(Bool)
    case number(Double)
    case string(String)
    case array([JSONValue])
    case object([String: JSONValue])

    /// Build from a Foundation graph (as produced by JSONSerialization of the wire).
    public static func from(_ any: Any?) -> JSONValue {
        guard let any = any, !(any is NSNull) else { return .null }
        if let n = any as? NSNumber {
            // A boolean NSNumber must not collapse into a number.
            if CFGetTypeID(n) == CFBooleanGetTypeID() { return .bool(n.boolValue) }
            return .number(n.doubleValue)
        }
        if let s = any as? String { return .string(s) }
        if let a = any as? [Any] { return .array(a.map(JSONValue.from)) }
        if let o = any as? [String: Any] {
            var out: [String: JSONValue] = [:]
            for (k, v) in o { out[k] = JSONValue.from(v) }
            return .object(out)
        }
        return .null
    }

    /// Lower to a Foundation graph for serialization back onto the wire.
    public func toFoundation() -> Any {
        switch self {
        case .null:
            return NSNull()
        case .bool(let b):
            return NSNumber(value: b)   // boolean-typed NSNumber → JSON true/false
        case .number(let d):
            // JSON has no NaN/Infinity; match JS `JSON.stringify` (→ null) so a non-finite value
            // from an adapter can never make the whole delivery batch fail to serialize.
            if !d.isFinite { return NSNull() }
            // Render integral values without a fractional part for clean JSON.
            if d.rounded() == d && abs(d) < 9.007e15 { return NSNumber(value: Int(d)) }
            return NSNumber(value: d)
        case .string(let s):
            return s
        case .array(let a):
            return a.map { $0.toFoundation() }
        case .object(let o):
            var out: [String: Any] = [:]
            for (k, v) in o { out[k] = v.toFoundation() }
            return out
        }
    }

    // Convenience accessors for adapter code.
    public var objectValue: [String: JSONValue]? { if case .object(let o) = self { return o }; return nil }
    // Numeric accessors narrowed to Int without trapping (see `clampedInt`).
    public var intValue: Int? { if case .number(let d) = self { return clampedInt(d) }; return nil }
    public var stringValue: String? { if case .string(let s) = self { return s }; return nil }
    public var numberValue: Double? { if case .number(let d) = self { return d }; return nil }
    public var boolValue: Bool? { if case .bool(let b) = self { return b }; return nil }
    public var arrayValue: [JSONValue]? { if case .array(let a) = self { return a }; return nil }
}

/// Narrow an IEEE double (as carried on the JSON wire) to `Int` without trapping.
/// `Int(Double)` in Swift is a *trapping* conversion: a value outside `Int`'s representable
/// range — or NaN — crashes the process unrecoverably. A hostile or malformed wire value
/// (e.g. `1e300` as a `pid`) must degrade to a sane in-range `Int`, never kill the host.
/// Matches the C++ hosts' `ja::reqInt` clamp semantics (NaN→0, saturate to min/max).
public func clampedInt(_ d: Double) -> Int {
    if d.isNaN { return 0 }
    if d >= Double(Int.max) { return Int.max }
    if d <= Double(Int.min) { return Int.min }
    return Int(d)
}
