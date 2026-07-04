// swift-tools-version:6.0
import PackageDescription

// The update adapter's SwiftPM package. Manifest at the adapter root (contract §3); the macOS
// sources are under hosts/macos/. Package identity `update` (the basename), distinct from the
// host and other adapters. Unlike an ordinary adapter, `update` is contract-coupled and never
// extracted: §7.1 signature verification and the §8 slot swap are host obligations it delegates
// to via UpdateHost.
let package = Package(
    name: "EngawaUpdate",
    platforms: [.macOS(.v13)],
    products: [
        .library(name: "EngawaUpdate", targets: ["EngawaUpdate"])
    ],
    dependencies: [
        .package(path: "../../hosts/macos-kit")
    ],
    targets: [
        .target(
            name: "EngawaUpdate",
            dependencies: [.product(name: "EngawaKit", package: "macos-kit")],
            path: "hosts/macos/Sources/EngawaUpdate"
        )
    ]
)
