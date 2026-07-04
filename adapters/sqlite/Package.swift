// swift-tools-version:6.0
import PackageDescription

// The sqlite adapter's SwiftPM package. The manifest lives at the adapter (repo) root — the
// standard location for a package consumed by commit hash (contract §3). The macOS sources are
// under hosts/macos/; windows/linux use their own build systems under hosts/{windows,linux}/.
// The root basename (`sqlite`) is the package identity, distinct from the host (`macos`) and
// other adapters, so multiple adapters compose without a basename collision. Swift 6 language
// mode (the default here) — the adapter API is Sendable.
let package = Package(
    name: "EngawaSQLite",
    platforms: [.macOS(.v13)],
    products: [
        .library(name: "EngawaSQLite", targets: ["EngawaSQLite"])
    ],
    dependencies: [
        .package(path: "../../hosts/macos-kit")
    ],
    targets: [
        .target(
            name: "EngawaSQLite",
            dependencies: [.product(name: "EngawaKit", package: "macos-kit")],
            path: "hosts/macos/Sources/EngawaSQLite",
            linkerSettings: [.linkedLibrary("sqlite3")]
        )
    ]
)
