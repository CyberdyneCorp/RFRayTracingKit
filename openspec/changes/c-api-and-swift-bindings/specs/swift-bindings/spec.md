## ADDED Requirements

### Requirement: Idiomatic Swift package over the C API
The project SHALL provide a SwiftPM package (`bindings/swift/`) that wraps the C API with idiomatic
Swift: a system-library target exposing `librftrace_c`, and a Swift layer presenting value types and
classes (Scene, Simulator, results) instead of raw pointers. Fallible C calls SHALL surface as Swift
`throws` (mapping status codes to a Swift `Error`), and handle lifetimes SHALL be managed by Swift
(RAII via `deinit`), so callers never call `_destroy` manually. The Swift API SHALL NOT expose raw C
pointers in its public surface.

#### Scenario: Swift run mirrors the C API
- **WHEN** a Swift caller builds a scene, adds a transmitter and receiver, and runs a simulation
- **THEN** it SHALL obtain the same results as the equivalent C API / C++ run, using Swift value types
  (arrays, doubles, structs) and `throws` for errors, with no manual memory management

#### Scenario: Errors surface as thrown Swift errors
- **WHEN** an underlying C call returns a non-OK status
- **THEN** the Swift wrapper SHALL throw a Swift `Error` carrying the C error message, not return an
  invalid object

### Requirement: Swift bindings build against the C library where a toolchain exists
The Swift package SHALL build and its tests SHALL run on a host with a Swift toolchain and a built
`librftrace_c`. Because this development host has no Swift toolchain, the Swift bindings are authored
to the C API contract and are UNVERIFIED here; they SHALL be validated where Swift is available and
SHALL NOT affect the default C++/C build.

#### Scenario: Package builds with a Swift toolchain
- **WHEN** `swift build` runs in `bindings/swift/` on a host with a Swift toolchain and `librftrace_c`
  available
- **THEN** the package SHALL compile and link, and `swift test` SHALL exercise the Swift wrapper
  against the C library

#### Scenario: Default build is unaffected
- **WHEN** the C++/C project is built without a Swift toolchain
- **THEN** the Swift package SHALL be inert (a separate SwiftPM tree, not part of the CMake/CTest
  build) and SHALL NOT break any existing build or test
