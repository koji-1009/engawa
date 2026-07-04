// swift-tools-version:6.0
import PackageDescription

// Swift 6 language mode. Two products (contract "static composition", docs/design.md):
//   EngawaHostCore — the host library: built-in namespaces + the contract-coupled `update`
//                    adapter. Every host has these; it takes an app's chosen adapters at runtime.
//   EngawaHost     — the reference/conformance executable: core + the `sqlite` reference adapter.
// Per-app hosts (built by the CLI) depend on EngawaHostCore and compile in only the adapters the
// app declares — `sqlite` is NOT baked into every host, only where an app asks for it.
let package = Package(
    name: "EngawaHost",
    platforms: [.macOS(.v13)],   // contract §9 OS floor
    products: [
        .library(name: "EngawaHostCore", targets: ["EngawaHostCore"]),
    ],
    dependencies: [
        .package(path: "../macos-kit"),
        .package(path: "../../adapters/update"),
        .package(path: "../../adapters/sqlite"),
    ],
    targets: [
        .target(
            name: "EngawaHostCore",
            dependencies: [
                .product(name: "EngawaKit", package: "macos-kit"),
                .product(name: "EngawaUpdate", package: "update"),   // contract-coupled (§7.1/§8)
            ],
            path: "Sources/EngawaHostCore"
        ),
        .executableTarget(
            name: "EngawaHost",
            dependencies: [
                "EngawaHostCore",
                .product(name: "EngawaKit", package: "macos-kit"),
                .product(name: "EngawaSQLite", package: "sqlite"),   // reference composition only
            ],
            path: "Sources/EngawaHost"
        ),
    ]
)
