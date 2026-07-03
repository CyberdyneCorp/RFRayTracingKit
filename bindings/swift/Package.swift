// swift-tools-version:5.7
//
// SwiftPM package for RFTraceKit, wrapping the stable C ABI (librftrace_c) with
// an idiomatic Swift API (value types, `throws`, RAII).
//
// UNVERIFIED: authored against the C API (include/rftrace/rftrace.h); it has NOT
// been compiled here because the development host has no Swift toolchain. Build
// where Swift + a built `librftrace_c` exist. The C header and library must be on
// the search paths — either install them, or point at an in-tree build, e.g.:
//
//   swift build \
//     -Xcc -I../../include \
//     -Xlinker -L../../build-capi \
//     -Xlinker -lrftrace_c
//
// (Configure the C library with `-DRFTRACE_ENABLE_C_API=ON`; see `just c-api`.)
import PackageDescription

let package = Package(
    name: "RFTrace",
    products: [
        .library(name: "RFTrace", targets: ["RFTrace"])
    ],
    targets: [
        // C ABI as a system-library module (module map over rftrace/rftrace.h,
        // links librftrace_c). Header/library search paths are supplied at build
        // time via -Xcc -I... / -Xlinker -L... (see the header comment above).
        .systemLibrary(name: "CRFTrace", path: "Sources/CRFTrace"),
        .target(
            name: "RFTrace",
            dependencies: ["CRFTrace"]
        ),
        .testTarget(
            name: "RFTraceTests",
            dependencies: ["RFTrace"]
        ),
    ]
)
