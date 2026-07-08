import Foundation
import EngawaHostCore
import EngawaKit
import EngawaSQLite

// TEMPORARY crash diagnostic: SWIFT_BACKTRACE is disabled for this (privileged) executable and the
// GH runners retain no crash reports, so install our own fatal-signal handler that dumps the crashing
// thread's symbolicated stack to stderr (inherited by the conformance runner) before re-raising.
// Remove once the intermittent boot SIGTRAP is located.
let crashHandler: @convention(c) (Int32) -> Void = { sig in
    let frames = Thread.callStackSymbols.joined(separator: "\n")
    FileHandle.standardError.write(Data("\n*** engawa host fatal signal \(sig) ***\n\(frames)\n".utf8))
    signal(sig, SIG_DFL)
    raise(sig)
}
for sig in [SIGTRAP, SIGABRT, SIGILL, SIGSEGV, SIGBUS] { signal(sig, crashHandler) }

// The reference / conformance host: the core plus the `sqlite` reference adapter (the conformance
// suite exercises sqlite, and this is the example composition). Per-app hosts the CLI generates
// pass their own declared adapters here instead.
EngawaRuntime.run(appAdapters: [SqliteAdapter()])
