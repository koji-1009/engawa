// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "EngawaHost",
    platforms: [.macOS(.v13)],   // contract §9 OS floor
    targets: [
        .executableTarget(
            name: "EngawaHost",
            path: "Sources/EngawaHost"
        )
    ]
)
