import Cocoa

// Entry point. Two modes:
//   default        — windowed app, loads app://app/index.html.
//   conformance    — offscreen, driven over a stdio control bridge by the runner
//                    (ENGAWA_CONFORMANCE=1).
let env = ProcessInfo.processInfo.environment
let mode = env["ENGAWA_CONFORMANCE"] == "1" ? "conformance" : "app"

// Autotest launches (the make-notes gate) run windowless like conformance.
let headless = mode == "conformance" || env["ENGAWA_AUTOTEST"] == "1"
let app = NSApplication.shared
app.setActivationPolicy(headless ? .accessory : .regular)

let host = EngawaHost(mode: mode, env: env)
host.boot()

app.run()
