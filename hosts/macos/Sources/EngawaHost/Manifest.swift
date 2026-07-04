import Foundation

// The app manifest (engawa.json): identity and the sidecar allowlist (contract §7.2),
// with CSP relaxations to come (§7.3). Loaded from the bundle root; sidecar paths are
// declared relative to it and MUST resolve inside it.
struct Manifest {
    let id: String
    let version: String
    let sidecars: [String]
    let bundleRoot: URL

    static func load(env: [String: String]) -> Manifest? {
        guard let root = env["ENGAWA_BUNDLE_ROOT"] else { return nil }
        let bundleRoot = URL(fileURLWithPath: root, isDirectory: true)
        let url = bundleRoot.appendingPathComponent("engawa.json")
        guard let data = try? Data(contentsOf: url),
              let obj = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else {
            return Manifest(id: "unknown", version: "0.0.0", sidecars: [], bundleRoot: bundleRoot)
        }
        return Manifest(
            id: obj["id"] as? String ?? "unknown",
            version: obj["version"] as? String ?? "0.0.0",
            sidecars: (obj["sidecars"] as? [Any])?.compactMap { $0 as? String } ?? [],
            bundleRoot: bundleRoot
        )
    }

    // Resolve a declared sidecar to an absolute executable path, or nil if not allowed (§7.2):
    // the command must be a declared entry and MUST resolve within the bundle root.
    func resolveSidecar(_ command: String) -> String? {
        guard sidecars.contains(command) else { return nil }
        let resolved = bundleRoot.appendingPathComponent(command).standardizedFileURL
        guard resolved.path.hasPrefix(bundleRoot.standardizedFileURL.path + "/") else { return nil }
        return resolved.path
    }
}
