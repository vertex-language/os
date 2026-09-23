// The 'os' repository: the running process, its environment, and the
// machine it runs on. Files and directories are the 'fs' repository's.
import PackageDescription

let package = Package(
    name: "os",
    platforms: [
        .macOS(.v13),
    ],
    products: [
        .library(name: "os/env", targets: ["os_env"]),
        .library(name: "os/process", targets: ["os_process"]),
        .library(name: "os/signal", targets: ["os_signal"]),
        .library(name: "os/user", targets: ["os_user"]),
        .library(name: "os/host", targets: ["os_host"]),
        .library(name: "os/term", targets: ["os_term"]),
        .library(name: "os/sys", targets: ["os_sys"]),
        .executable(name: "check", targets: ["check"]),
        .executable(name: "os-run", targets: ["example_run"]),
    ],
    targets: [
        // The operating system's calls, as a C ABI. The only native code
        // in the repository.
        .target(
            name: "cos",
            path: "cos",
            publicHeadersPath: "include"
        ),
        // cos as Vertex sees it, and the helpers every package shares.
        // Internal: nothing outside this repository imports it.
        .target(
            name: "os_sys",
            dependencies: ["cos"],
            path: "sys"
        ),
        .target(name: "os_env", dependencies: ["os_sys"], path: "env"),
        .target(name: "os_host", dependencies: ["os_sys"], path: "host"),
        .target(name: "os_user", dependencies: ["os_sys", "os_env", "os_host"], path: "user"),
        .target(name: "os_term", dependencies: ["os_sys", "os_env"], path: "term"),
        .target(name: "os_signal", dependencies: ["os_sys"], path: "signal"),
        .target(name: "os_process", dependencies: ["os_sys", "os_env", "os_host", "os_signal"], path: "process"),
        .executableTarget(
            name: "check",
            dependencies: ["os_env", "os_process", "os_signal", "os_user", "os_host", "os_term"],
            path: "tests/check"
        ),
        .executableTarget(
            name: "example_run",
            dependencies: ["os_process"],
            path: "examples/run"
        ),
    ],
    cxxLanguageStandard: "c++20"
)
