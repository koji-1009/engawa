import Foundation

// Host services the `update` adapter delegates to (contract §7.1, §8). Update is
// contract-coupled: signature verification against the embedded trust root and the atomic
// A/B slot swap are HOST obligations (CLAUDE.md), not adapter code. The adapter owns delivery
// (manifest transport, payload fetch, compatibility policy); the host owns trust and slots.
public protocol UpdateHost: Sendable {
    /// Current slot state: `{ currentSlot, version, hasPending, bootingSlot }`.
    func status() -> JSONValue

    /// §7.1 + §8: verify the payload (its content hash matches `hashHex`, and `signatureB64`
    /// is a valid signature over that hash under the embedded trust root) and, only then,
    /// unpack it into the non-live slot and reserve adoption (write `pending`, the single
    /// atomic commit point). Rejects unverified payloads with `ESIGNATURE`/`EHASH`.
    func stageAppUpdate(payloadPath: String, hashHex: String, signatureB64: String, version: String) throws

    /// §8 full-update / §153: verify a base installer against the embedded trust root (same hash +
    /// signature check as `stageAppUpdate`) WITHOUT unpacking it into a slot, so the adapter only
    /// announces `readyToInstall` once the installer is verified. Rejects with `ESIGNATURE`/`EHASH`.
    func verifyBaseInstaller(payloadPath: String, hashHex: String, signatureB64: String) throws

    /// The app initialized successfully on the pending slot: adopt it (switch `current`,
    /// clear `pending`/`health`). A required command of the `update` namespace (§8).
    func confirmBoot()

    /// Conformance-only: re-run the boot-time slot decision (attempts++/rollback) without
    /// restarting the process, so the suite can exercise relaunch behavior.
    func relaunchForTest() -> JSONValue
}
