import Foundation
import EngawaKit

// The `fs` namespace (spec/commands/fs.md). Text only; binary rides app://io (§5a).
// Not sandboxed in v1 (§7): paths are used as given.
struct FsAdapter: Adapter {
    let namespace = "fs"
    private let fm = FileManager.default

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        let obj = args.objectValue ?? [:]

        switch cmd {
        case "readTextFile":
            let p = try path(obj)
            var isDir: ObjCBool = false
            guard fm.fileExists(atPath: p, isDirectory: &isDir) else { throw AdapterError("ENOENT", "no such file: \(p)") }
            if isDir.boolValue { throw AdapterError("EISDIR", "is a directory: \(p)") }
            do { return .string(try String(contentsOfFile: p, encoding: .utf8)) }
            catch { throw AdapterError("EIO", error.localizedDescription) }

        case "writeTextFile":
            let p = try path(obj)
            guard let contents = obj["contents"]?.stringValue else { throw AdapterError("EINVAL", "contents required") }
            let parent = (p as NSString).deletingLastPathComponent
            var parentIsDir: ObjCBool = false
            guard fm.fileExists(atPath: parent, isDirectory: &parentIsDir), parentIsDir.boolValue else {
                throw AdapterError("ENOENT", "parent directory does not exist: \(parent)")
            }
            do { try contents.write(toFile: p, atomically: true, encoding: .utf8); return .null }
            catch { throw AdapterError("EIO", error.localizedDescription) }

        case "exists":
            return .bool(fm.fileExists(atPath: try path(obj)))

        case "mkdir":
            let p = try path(obj)
            let recursive = obj["recursive"]?.boolValue ?? false
            if fm.fileExists(atPath: p) {
                if recursive { return .null }
                throw AdapterError("EEXIST", "already exists: \(p)")
            }
            do { try fm.createDirectory(atPath: p, withIntermediateDirectories: recursive); return .null }
            catch { throw AdapterError("EIO", error.localizedDescription) }

        case "remove":
            let p = try path(obj)
            let recursive = obj["recursive"]?.boolValue ?? false
            var isDir: ObjCBool = false
            guard fm.fileExists(atPath: p, isDirectory: &isDir) else { throw AdapterError("ENOENT", "no such path: \(p)") }
            if isDir.boolValue && !recursive {
                let entries = (try? fm.contentsOfDirectory(atPath: p)) ?? []
                if !entries.isEmpty { throw AdapterError("ENOTEMPTY", "directory not empty: \(p)") }
            }
            do { try fm.removeItem(atPath: p); return .null }
            catch { throw AdapterError("EIO", error.localizedDescription) }

        case "readDir":
            let p = try path(obj)
            var isDir: ObjCBool = false
            guard fm.fileExists(atPath: p, isDirectory: &isDir) else { throw AdapterError("ENOENT", "no such path: \(p)") }
            guard isDir.boolValue else { throw AdapterError("ENOTDIR", "not a directory: \(p)") }
            let names = (try? fm.contentsOfDirectory(atPath: p)) ?? []
            return .array(names.map { name in
                var childIsDir: ObjCBool = false
                _ = fm.fileExists(atPath: p + "/" + name, isDirectory: &childIsDir)
                return .object(["name": .string(name), "isDirectory": .bool(childIsDir.boolValue)])
            })

        case "stat":
            let p = try path(obj)
            var isDir: ObjCBool = false
            guard fm.fileExists(atPath: p, isDirectory: &isDir) else { throw AdapterError("ENOENT", "no such path: \(p)") }
            let attrs = (try? fm.attributesOfItem(atPath: p)) ?? [:]
            let size = (attrs[.size] as? NSNumber)?.doubleValue ?? 0
            let mtimeMs = ((attrs[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0) * 1000
            return .object([
                "type": .string(isDir.boolValue ? "directory" : "file"),
                "size": .number(size),
                "modified": .number(mtimeMs),
            ])

        default:
            throw AdapterError("ENOSYS", "unknown command: fs.\(cmd)")
        }
    }

    private func path(_ obj: [String: JSONValue]) throws -> String {
        guard let p = obj["path"]?.stringValue, !p.isEmpty else { throw AdapterError("EINVAL", "path required") }
        return p
    }
}
