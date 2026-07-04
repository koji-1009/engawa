import Foundation
import EngawaKit

// The `update` namespace (adapters/update/spec.md). Delivery and policy live here; trust
// (§7.1) and the atomic slot swap (§8) are host obligations reached through UpdateHost.
public final class UpdateAdapter: Adapter, @unchecked Sendable {
    public let namespace = "update"
    private let host: UpdateHost
    private var emitter: EventEmitter?   // set once in attach

    public init(host: UpdateHost) {
        self.host = host
    }

    public func attach(_ emitter: EventEmitter) {
        self.emitter = emitter
    }

    public func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        let obj = args.objectValue ?? [:]

        switch cmd {
        case "status":
            return host.status()

        case "confirmBoot":
            host.confirmBoot()
            return .null

        case "stageAppUpdate":
            guard let path = obj["payloadPath"]?.stringValue,
                  let hash = obj["hash"]?.stringValue,
                  let sig = obj["signature"]?.stringValue else {
                throw AdapterError("EINVAL", "payloadPath, hash and signature required")
            }
            let version = obj["version"]?.stringValue ?? "0.0.0"
            try host.stageAppUpdate(payloadPath: path, hashHex: hash, signatureB64: sig, version: version)
            return .object(["staged": .bool(true)])

        case "evaluate":
            return evaluate(obj)

        case "install":
            // full-update handoff: the OS-native replacement is out of contract scope (§8).
            return .object(["handoff": .bool(true)])

        case "__relaunch":   // conformance-only testability hook
            return host.relaunchForTest()

        default:
            throw AdapterError("ENOSYS", "unknown command: update.\(cmd)")
        }
    }

    // §8 compatibility rule: an app-update applies alone iff the running base satisfies the
    // manifest's contractRequired and serves every capabilitiesRequired; else a full-update is
    // needed (base first). `provided` carries the running base's facts.
    private func evaluate(_ obj: [String: JSONValue]) -> JSONValue {
        let app = obj["manifest"]?.objectValue?["app"]?.objectValue ?? [:]
        let contractRequired = app["contractRequired"]?.stringValue ?? "1.0"
        let capsRequired = (app["capabilitiesRequired"]?.arrayValue ?? []).compactMap { $0.stringValue }
        let version = app["version"]?.stringValue ?? "0.0.0"

        let provided = obj["provided"]?.objectValue ?? [:]
        let contractProvided = provided["contractProvided"]?.stringValue ?? "1.0"
        let capsProvided = Set((provided["capabilities"]?.arrayValue ?? []).compactMap { $0.stringValue })

        let contractOK = contractRequired == contractProvided   // exact major.minor match for v1
        let capsOK = capsRequired.allSatisfy { capsProvided.contains($0) }
        let mode = (contractOK && capsOK) ? "app-update" : "full-update"

        if mode == "full-update" {
            emitter?.emit("update.readyToInstall", .object(["version": .string(version)]))
        }
        return .object(["mode": .string(mode), "version": .string(version)])
    }
}
