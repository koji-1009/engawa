// swift-tools-version:6.0
import PackageDescription

// EngawaKit — the macOS host's adapter SDK (contract §3). Built-in namespaces and external
// adapters both depend on this; it holds the Adapter protocol, the JSON value type, the event
// emitter, and the router. A sibling of the host package (not nested, and a distinct basename)
// so an adapter can depend on it without cycling back into the host executable. Swift 6
// language mode: the adapter API is Sendable.
let package = Package(
    name: "EngawaKit",
    platforms: [.macOS(.v13)],
    products: [
        .library(name: "EngawaKit", targets: ["EngawaKit"])
    ],
    targets: [
        .target(name: "EngawaKit", path: "Sources/EngawaKit")
    ]
)
