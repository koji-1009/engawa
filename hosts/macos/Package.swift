// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "EngawaHost",
    platforms: [.macOS(.v13)],   // contract §9 OS floor
    dependencies: [
        .package(path: "EngawaKit"),
    ],
    targets: [
        .executableTarget(
            name: "EngawaHost",
            dependencies: [
                .product(name: "EngawaKit", package: "EngawaKit"),
            ],
            path: "Sources/EngawaHost"
        )
    ]
)
