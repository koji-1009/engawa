// swift-tools-version:5.9
import PackageDescription

// EngawaKit — the macOS host's adapter SDK (contract §3). Built-in namespaces and external
// adapters both depend on this; it holds the Adapter protocol, the JSON value type, the event
// emitter, and the router. Kept a separate package so an adapter repo can depend on it without
// depending on the host executable (which in turn depends on the adapters — no cycle).
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
