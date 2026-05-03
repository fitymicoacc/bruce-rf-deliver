// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "LilDemo",
    platforms: [.iOS(.v13)],
    targets: [
        .systemLibrary(
            name: "CLil",
            path: "Sources/CLil"
        ),
        .executableTarget(
            name: "LilDemo",
            dependencies: ["CLil"],
            path: "Sources/LilDemo"
        ),
    ]
)
