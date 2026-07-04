import Foundation
import EngawaKit

// Resolved standard directories for the running app (spec/commands/path.md).
// Under ENGAWA_DATA_ROOT (conformance / isolated runs) everything nests in one root;
// otherwise the macOS-conventional locations, scoped by app id.
struct AppDirs {
    let appData: String
    let appConfig: String
    let appCache: String
    let home: String
    let temp: String

    static func resolve(env: [String: String]) -> AppDirs {
        let home = NSHomeDirectory()
        let temp = NSTemporaryDirectory()
        if let root = env["ENGAWA_DATA_ROOT"] {
            return AppDirs(appData: root + "/data", appConfig: root + "/config",
                           appCache: root + "/cache", home: home, temp: temp)
        }
        let appId = env["ENGAWA_APP_ID"] ?? "dev.engawa.reference"
        let support = home + "/Library/Application Support/" + appId
        let caches = home + "/Library/Caches/" + appId
        return AppDirs(appData: support + "/data", appConfig: support + "/config",
                       appCache: caches, home: home, temp: temp)
    }
}

struct PathAdapter: Adapter {
    let namespace = "path"
    let dirs: AppDirs

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        switch cmd {
        case "appData":   return .string(try ensured(dirs.appData))
        case "appConfig": return .string(try ensured(dirs.appConfig))
        case "appCache":  return .string(try ensured(dirs.appCache))
        case "home":      return .string(dirs.home)
        case "temp":      return .string(dirs.temp)
        default:          throw AdapterError("ENOSYS", "unknown command: path.\(cmd)")
        }
    }

    private func ensured(_ path: String) throws -> String {
        do {
            try FileManager.default.createDirectory(atPath: path, withIntermediateDirectories: true)
            return path
        } catch {
            throw AdapterError("EIO", "could not create \(path): \(error.localizedDescription)")
        }
    }
}
