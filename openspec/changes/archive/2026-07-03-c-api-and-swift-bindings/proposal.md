## Why

The engine is C++-only plus Python bindings. To be embeddable from other languages (Swift/iOS/macOS
apps, and any FFI consumer — Rust, Go, C#, C), it needs a **stable C ABI**. Swift interoperates with
C, not C++, so a C API is the prerequisite for Swift bindings and simultaneously unlocks every other
FFI. This adds that C API and an idiomatic Swift package on top of it.

## What Changes

- **C API** (`extern "C"`, compiled as C++ against the existing core; no core changes): opaque
  handles for `Scene` and `Simulator`; functions to build a scene (materials, meshes from float
  arrays, transmitters/receivers), configure a settings struct, run `run`/`runCoverage`/`runRoute`,
  and read results (per-receiver power/path-loss/delay/SINR, coverage arrays, route samples). The
  boundary NEVER throws — every call returns a status code, with a thread-local error-message
  accessor. Clear memory ownership (caller creates/destroys handles; result buffers copied out or
  accessed by pointer+length with documented lifetime). A `rftrace_version()` and an ABI version.
  Built behind `RFTRACE_ENABLE_C_API` (default OFF), producing a `librftrace_c` shared/static lib and
  an installed `rftrace/rftrace.h` C header. A C (not C++) test links it and exercises the ABI.
- **Swift bindings**: a SwiftPM package (`bindings/swift/`) exposing the C API as a system library
  target plus an idiomatic Swift layer (Swift structs/classes wrapping the handles, `throws` mapped
  from status codes, `Array`/`Double` result types, RAII via `deinit`). Authored against the C API;
  **UNVERIFIED on this host** (no Swift toolchain on Linux) — it compiles where a Swift toolchain +
  the built `librftrace_c` exist, mirroring how the CUDA backend was authored before NVIDIA hardware
  was available.

## Capabilities

### New Capabilities
- `c-api`: a stable, no-throw `extern "C"` ABI over the scene + simulator + results, with explicit
  memory ownership, status-code/error-string handling, and versioning — buildable and C-testable,
  and the foundation for all non-C++ bindings.
- `swift-bindings`: an idiomatic SwiftPM package wrapping the C API (throwing Swift API, value types,
  RAII), authored to the C API contract; verified where a Swift toolchain is available.

### Modified Capabilities
<!-- None. The C++ core, its public headers, and existing behavior are unchanged; the C API is a
     new additive wrapper layer and the Swift package sits entirely on top of it. -->

## Impact

- **Code**: new `bindings/c/` (the `extern "C"` header `include/rftrace/rftrace.h` + implementation
  `bindings/c/rftrace_c.cpp`), new `bindings/swift/` (SwiftPM package), CMake wiring behind
  `RFTRACE_ENABLE_C_API`, a C test target, and a `just c-api` recipe.
- **Public API**: additive only — a new C header and library; the C++ API is untouched.
- **Build/deps**: no new third-party deps for the C API; Swift needs a Swift toolchain (external,
  not required for the default build). Not part of the default `ci` recipe.
- **Tests**: a C ABI test (create scene → run → read results; error paths; no-leak under a sanitizer)
  gates the C API on Linux. Swift has its own package tests, runnable only with a Swift toolchain.
- **Risk**: ABI/ownership design and no-throw discipline at the boundary (contained by the C test +
  sanitizer); the Swift layer is unverified here (clearly marked, like the pre-hardware CUDA backend).
