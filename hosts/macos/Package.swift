// swift-tools-version:6.0
import PackageDescription

// Swift 6 language mode. Package identities are the (now distinct) directory basenames:
// host `macos`, SDK `macos-kit`, adapter `sqlite`.
let package = Package(
    name: "EngawaHost",
    platforms: [.macOS(.v13)],   // contract §9 OS floor
    dependencies: [
        .package(path: "../macos-kit"),
        .package(path: "../../adapters/sqlite"),
    ],
    targets: [
        .executableTarget(
            name: "EngawaHost",
            dependencies: [
                .product(name: "EngawaKit", package: "macos-kit"),
                .product(name: "EngawaSQLite", package: "sqlite"),
            ],
            path: "Sources/EngawaHost"
        )
    ]
)
