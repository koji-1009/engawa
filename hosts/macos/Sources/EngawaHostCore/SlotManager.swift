import Foundation
import CryptoKit
import EngawaKit

// The A/B slot machinery and update trust root — HOST obligations (contract §7.1, §8,
// CLAUDE.md). The `update` adapter delegates here via UpdateHost. Layout under <base>:
//
//   slots/{a,b}/   asset trees; exactly one is live
//   current        pointer to the live slot (one-line file)
//   pending        slot awaiting adoption, if any (the single atomic commit point)
//   health         { bootingSlot, attempts }
//   version        adopted app version
//
// The app:// asset root is `liveSlotDir()`, not a fixed directory.
//
// Conformance note: the app-update payload is modelled as a single file that becomes the new
// slot's index.html. This exercises the full §7.1 verify + §8 swap/confirm/rollback machinery;
// multi-file payloads (tar) are a later refinement.
final class SlotManager: UpdateHost, @unchecked Sendable {
    private let base: URL
    private let seed: URL?
    private let trustRoot: Curve25519.Signing.PublicKey?
    private let lock = NSLock()
    private var bootedSlot = "a"

    init(base: URL, seed: URL?, trustRootB64: String?) {
        self.base = base
        self.seed = seed
        self.trustRoot = trustRootB64
            .flatMap { Data(base64Encoded: $0) }
            .flatMap { try? Curve25519.Signing.PublicKey(rawRepresentation: $0) }
    }

    // MARK: paths
    private var slotsDir: URL { base.appendingPathComponent("slots") }
    private func slotDir(_ name: String) -> URL { slotsDir.appendingPathComponent(name) }
    private var currentFile: URL { base.appendingPathComponent("current") }
    private var pendingFile: URL { base.appendingPathComponent("pending") }
    private var healthFile: URL { base.appendingPathComponent("health") }
    private var versionFile: URL { base.appendingPathComponent("version") }
    private var pendingVersionFile: URL { base.appendingPathComponent("version.pending") }

    // MARK: boot

    /// Ensure the layout exists, apply the boot-time slot decision, return the live slot dir.
    func boot() -> URL {
        lock.lock(); defer { lock.unlock() }
        ensureInit()
        return decideLiveSlot()
    }

    func liveSlotDir() -> URL {
        lock.lock(); defer { lock.unlock() }
        return slotDir(bootedSlot)
    }

    private func ensureInit() {
        let fm = FileManager.default
        try? fm.createDirectory(at: slotDir("a"), withIntermediateDirectories: true)
        try? fm.createDirectory(at: slotDir("b"), withIntermediateDirectories: true)
        if !fm.fileExists(atPath: currentFile.path) {
            if let seed = seed { copyContents(of: seed, into: slotDir("a")) }
            writeAtomic(currentFile, "a")
            writeAtomic(versionFile, "0.0.0")
        }
    }

    // Decide which slot boots. Booting a pending slot bumps attempts; after 2 unconfirmed
    // attempts the pending payload is discarded and the previous slot boots (auto-rollback).
    private func decideLiveSlot() -> URL {
        let current = readString(currentFile) ?? "a"
        guard let pending = readString(pendingFile) else {
            bootedSlot = current
            return slotDir(current)
        }
        var attempts = readHealthAttempts() + 1
        if attempts > 2 {
            try? FileManager.default.removeItem(at: pendingFile)
            try? FileManager.default.removeItem(at: healthFile)
            try? FileManager.default.removeItem(at: pendingVersionFile)
            bootedSlot = current
            return slotDir(current)
        }
        writeHealth(bootingSlot: pending, attempts: attempts)
        bootedSlot = pending
        return slotDir(pending)
    }

    // MARK: UpdateHost

    func status() -> JSONValue {
        lock.lock(); defer { lock.unlock() }
        return statusLocked()
    }

    private func statusLocked() -> JSONValue {
        let pending = readString(pendingFile)
        return .object([
            "currentSlot": .string(readString(currentFile) ?? "a"),
            "bootingSlot": .string(bootedSlot),
            "version": .string(readString(versionFile) ?? "0.0.0"),
            "hasPending": .bool(pending != nil),
            "pendingSlot": pending.map { JSONValue.string($0) } ?? .null,
        ])
    }

    func stageAppUpdate(payloadPath: String, hashHex: String, signatureB64: String, version: String) throws {
        lock.lock(); defer { lock.unlock() }

        // §7.1: verify BEFORE anything is placed under the app:// root.
        guard let payload = try? Data(contentsOf: URL(fileURLWithPath: payloadPath)) else {
            throw AdapterError("ENOENT", "payload not found: \(payloadPath)")
        }
        let digest = SHA256.hash(data: payload)
        let computedHex = digest.map { String(format: "%02x", $0) }.joined()
        guard computedHex == hashHex.lowercased() else { throw AdapterError("EHASH", "payload hash mismatch") }
        guard let trustRoot = trustRoot else { throw AdapterError("ESIGNATURE", "no trust root embedded") }
        guard let sig = Data(base64Encoded: signatureB64) else { throw AdapterError("ESIGNATURE", "malformed signature") }
        guard trustRoot.isValidSignature(sig, for: Data(digest)) else {
            throw AdapterError("ESIGNATURE", "signature verification failed")
        }

        // Verified: unpack the payload into the non-LIVE slot (§8). The target is the opposite of
        // the booted slot, never `current`: while a pending slot is booted-but-unconfirmed,
        // `bootedSlot != current`, and choosing by `current` would target the live slot and
        // destroy the assets app:// is serving. Opposite-of-booted is always the safe scratch slot.
        let target = bootedSlot == "a" ? "b" : "a"
        let targetDir = slotDir(target)
        try? FileManager.default.removeItem(at: targetDir)
        try FileManager.default.createDirectory(at: targetDir, withIntermediateDirectories: true)
        try extractTar(URL(fileURLWithPath: payloadPath), into: targetDir)
        writeAtomic(pendingVersionFile, version)

        // The single atomic commit point: reserve adoption.
        writeAtomic(pendingFile, target)
    }

    func confirmBoot() {
        lock.lock(); defer { lock.unlock() }
        guard let pending = readString(pendingFile), pending == bootedSlot else { return }
        writeAtomic(currentFile, pending)
        if let v = readString(pendingVersionFile) { writeAtomic(versionFile, v) }
        try? FileManager.default.removeItem(at: pendingFile)
        try? FileManager.default.removeItem(at: healthFile)
        try? FileManager.default.removeItem(at: pendingVersionFile)
    }

    func relaunchForTest() -> JSONValue {
        lock.lock(); defer { lock.unlock() }
        _ = decideLiveSlot()
        return statusLocked()
    }

    // MARK: file helpers

    private func readString(_ url: URL) -> String? {
        (try? String(contentsOf: url, encoding: .utf8))?.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func writeAtomic(_ url: URL, _ contents: String) {
        try? contents.write(to: url, atomically: true, encoding: .utf8)   // temp + rename
    }

    private func readHealthAttempts() -> Int {
        guard let data = try? Data(contentsOf: healthFile),
              let obj = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else { return 0 }
        return obj["attempts"] as? Int ?? 0
    }

    private func writeHealth(bootingSlot: String, attempts: Int) {
        let obj: [String: Any] = ["bootingSlot": bootingSlot, "attempts": attempts]
        if let data = try? JSONSerialization.data(withJSONObject: obj) {
            try? data.write(to: healthFile, options: .atomic)
        }
    }

    private func extractTar(_ tar: URL, into dir: URL) throws {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/tar")
        p.arguments = ["-xf", tar.path, "-C", dir.path]
        do { try p.run() } catch { throw AdapterError("EIO", "cannot run tar: \(error.localizedDescription)") }
        p.waitUntilExit()
        if p.terminationStatus != 0 { throw AdapterError("EIO", "tar extraction failed (\(p.terminationStatus))") }
    }

    private func copyContents(of src: URL, into dst: URL) {
        let fm = FileManager.default
        try? fm.createDirectory(at: dst, withIntermediateDirectories: true)
        guard let items = try? fm.contentsOfDirectory(at: src, includingPropertiesForKeys: nil) else { return }
        for item in items {
            let target = dst.appendingPathComponent(item.lastPathComponent)
            try? fm.removeItem(at: target)
            try? fm.copyItem(at: item, to: target)
        }
    }
}
