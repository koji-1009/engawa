import Foundation
import EngawaKit
import SQLite3

// The `sqlite` reference adapter (adapters/sqlite/spec.md). Request-driven, local, durable —
// the first consumer of the adapter SDK from outside the built-in set. Text/number values
// over the wire; blobs are out of the v1 text scope.
private let SQLITE_TRANSIENT = unsafeBitCast(-1, to: sqlite3_destructor_type.self)

public final class SqliteAdapter: Adapter, @unchecked Sendable {
    public let namespace = "sqlite"

    private let lock = NSLock()   // guards dbs/nextHandle; @unchecked Sendable rests on it
    private var dbs: [Int: OpaquePointer] = [:]
    private var nextHandle = 1

    public init() {}

    public func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        try perform(cmd, args)
    }

    // Synchronous so NSLock is legal (locks are unavailable from async contexts in Swift 6).
    // SQLite handles are not concurrency-safe, so the whole operation is serialized.
    private func perform(_ cmd: String, _ args: JSONValue) throws -> JSONValue {
        lock.lock(); defer { lock.unlock() }
        switch cmd {
        case "open":    return try open(args)
        case "execute": return try run(args, query: false)
        case "query":   return try run(args, query: true)
        case "close":   return try close(args)
        default:        throw AdapterError("ENOSYS", "unknown command: sqlite.\(cmd)")
        }
    }

    private func open(_ args: JSONValue) throws -> JSONValue {
        guard let path = args.objectValue?["path"]?.stringValue, !path.isEmpty else {
            throw AdapterError("EINVAL", "path required")
        }
        var db: OpaquePointer?
        if sqlite3_open(path, &db) != SQLITE_OK {
            let msg = db.map { String(cString: sqlite3_errmsg($0)) } ?? "open failed"
            if let db = db { sqlite3_close(db) }
            throw AdapterError("ESQLITE", msg)
        }
        let handle = nextHandle
        nextHandle += 1
        dbs[handle] = db
        return .object(["db": .number(Double(handle))])
    }

    private func run(_ args: JSONValue, query: Bool) throws -> JSONValue {
        let obj = args.objectValue ?? [:]
        guard let handle = obj["db"]?.numberValue else { throw AdapterError("EINVAL", "db required") }
        guard let sql = obj["sql"]?.stringValue else { throw AdapterError("EINVAL", "sql required") }
        guard let db = dbs[Int(handle)] else { throw AdapterError("EBADF", "unknown db handle: \(Int(handle))") }

        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK, let stmt = stmt else {
            throw AdapterError("ESQLITE", String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }

        try bind(obj["params"]?.arrayValue ?? [], to: stmt)

        if query {
            var rows: [JSONValue] = []
            let cols = Int(sqlite3_column_count(stmt))
            while sqlite3_step(stmt) == SQLITE_ROW {
                var row: [String: JSONValue] = [:]
                for c in 0..<cols {
                    let name = String(cString: sqlite3_column_name(stmt, Int32(c)))
                    row[name] = columnValue(stmt, Int32(c))
                }
                rows.append(.object(row))
            }
            return .object(["rows": .array(rows)])
        } else {
            let rc = sqlite3_step(stmt)
            guard rc == SQLITE_DONE || rc == SQLITE_ROW else {
                throw AdapterError("ESQLITE", String(cString: sqlite3_errmsg(db)))
            }
            return .object([
                "changes": .number(Double(sqlite3_changes(db))),
                "lastInsertRowid": .number(Double(sqlite3_last_insert_rowid(db))),
            ])
        }
    }

    private func close(_ args: JSONValue) throws -> JSONValue {
        guard let handle = args.objectValue?["db"]?.numberValue else { throw AdapterError("EINVAL", "db required") }
        guard let db = dbs.removeValue(forKey: Int(handle)) else { throw AdapterError("EBADF", "unknown db handle") }
        sqlite3_close(db)
        return .null
    }

    private func bind(_ params: [JSONValue], to stmt: OpaquePointer) throws {
        for (i, p) in params.enumerated() {
            let idx = Int32(i + 1)
            switch p {
            case .null:
                sqlite3_bind_null(stmt, idx)
            case .bool(let b):
                sqlite3_bind_int64(stmt, idx, b ? 1 : 0)
            case .number(let d):
                if d.rounded() == d && abs(d) < 9.007e15 { sqlite3_bind_int64(stmt, idx, Int64(d)) }
                else { sqlite3_bind_double(stmt, idx, d) }
            case .string(let s):
                sqlite3_bind_text(stmt, idx, s, -1, SQLITE_TRANSIENT)
            default:
                throw AdapterError("EINVAL", "unsupported parameter type at index \(i)")
            }
        }
    }

    private func columnValue(_ stmt: OpaquePointer, _ c: Int32) -> JSONValue {
        switch sqlite3_column_type(stmt, c) {
        case SQLITE_INTEGER: return .number(Double(sqlite3_column_int64(stmt, c)))
        case SQLITE_FLOAT:   return .number(sqlite3_column_double(stmt, c))
        case SQLITE_TEXT:    return .string(String(cString: sqlite3_column_text(stmt, c)))
        default:             return .null   // NULL, and BLOB (out of v1 text scope)
        }
    }
}
