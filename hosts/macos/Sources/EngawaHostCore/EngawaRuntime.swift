import Cocoa
import EngawaKit

// The composition entry point. A host executable — the reference `EngawaHost`, or a per-app host
// the CLI generates — calls `EngawaRuntime.run(appAdapters:)` with the adapters that app declared.
// The core always provides the built-in namespaces and the contract-coupled `update` adapter;
// `appAdapters` are the app's extra, statically-composed adapters (e.g. sqlite).
public enum EngawaRuntime {
    @MainActor
    public static func run(appAdapters: [any Adapter]) {
        let env = ProcessInfo.processInfo.environment
        let mode = env["ENGAWA_CONFORMANCE"] == "1" ? "conformance" : "app"

        // Autotest launches (the make-notes gate) run windowless like conformance.
        let headless = mode == "conformance" || env["ENGAWA_AUTOTEST"] == "1"
        let app = NSApplication.shared
        app.setActivationPolicy(headless ? .accessory : .regular)

        let host = EngawaHost(mode: mode, env: env, appAdapters: appAdapters)
        host.boot()

        app.run()
    }
}
