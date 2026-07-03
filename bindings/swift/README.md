# RFTrace — Swift bindings

Idiomatic Swift wrapper over the RFTraceKit C ABI (`librftrace_c`): value types,
`throws` error handling, and RAII (handles freed in `deinit`). No raw pointers in
the public API.

> **UNVERIFIED build.** This package is authored against the C header
> (`include/rftrace/rftrace.h`) and has **not** been compiled — the development
> host has no Swift toolchain. Build and test it where a Swift toolchain and a
> built `librftrace_c` are available (macOS, or Linux with Swift). It is a
> standalone SwiftPM tree and is **not** part of the CMake/CTest build, so it
> cannot affect the C/C++ build.

## Prerequisites

Build the C library with the C API enabled (from the repo root):

```sh
just c-api          # or: cmake -S . -B build-capi -DRFTRACE_ENABLE_C_API=ON && cmake --build build-capi
```

This produces `librftrace_c` and the C header under `include/rftrace/rftrace.h`.

## Build & test

The C header and library search paths are supplied at build time. From
`bindings/swift/`:

```sh
swift build \
  -Xcc -I../../include \
  -Xlinker -L../../build-capi \
  -Xlinker -lrftrace_c

swift test \
  -Xcc -I../../include \
  -Xlinker -L../../build-capi \
  -Xlinker -lrftrace_c
```

(Adjust the paths if you installed the header/library to a system prefix, in
which case the `-Xcc`/`-Xlinker` flags may be unnecessary.)

## Usage

```swift
import RFTrace

let scene = try Scene()
try scene.addTransmitter(id: "tx", position: Vec3(0, 0, 0), frequencyHz: 3.5e9, powerDbm: 43)
try scene.addReceiver(id: "rx", position: Vec3(100, 0, 0))

var settings = Settings()
settings.maxReflections = 1
settings.threadCount = 0            // 0 = all cores; 1 = serial

let sim = try Simulator(settings)
let result = try sim.run(scene)
for rx in result.receivers {
    print(rx.id, rx.receivedPowerDbm, "dBm", rx.hasSignal ? "" : "(no signal)")
}

// Coverage grid
let grid = Grid(origin: Vec3(0, 0, 0), cellSize: 5, cols: 40, rows: 40, height: 1.5)
let cov = try sim.runCoverage(scene, grid: grid)
print("center cell:", cov.powerAt(row: 20, col: 20))
```

Errors from the C layer surface as `throws RFTraceError` carrying the C message.
All handles are released automatically.
