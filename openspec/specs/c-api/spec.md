# c-api Specification

## Purpose
A stable, no-throw extern "C" ABI (librftrace_c) over the scene, simulator, and results — opaque handles, status/error codes, explicit ownership, count-then-fill result copies, and versioning — wrapping the C++ core without changing it, so any FFI-capable language can drive RFTraceKit.
## Requirements
### Requirement: Stable extern "C" ABI over the core
The library SHALL provide a C API (`extern "C"`, C-header `rftrace/rftrace.h`) exposing scene
construction, simulation, and result reading over the existing C++ core, compiled behind
`RFTRACE_ENABLE_C_API` (default OFF) into a `librftrace_c` library. The C++ core, its public headers,
and its behavior SHALL be unchanged; the C API is a thin additive wrapper. The API SHALL expose a
version query (`rftrace_version()`) and a numeric ABI version so consumers can check compatibility.

#### Scenario: C header is C-compatible
- **WHEN** `rftrace/rftrace.h` is included from a C translation unit and linked against `librftrace_c`
- **THEN** it SHALL compile as C (no C++ constructs in the header) and expose the documented
  functions, opaque handle typedefs, POD settings/result structs, and status enum

#### Scenario: Core is unchanged when the C API is off
- **WHEN** the project is built with `RFTRACE_ENABLE_C_API=OFF` (default)
- **THEN** the C API SHALL NOT be compiled and the existing C++ library and test suite SHALL build and
  pass unchanged

### Requirement: The C boundary never throws
No C API function SHALL allow a C++ exception to propagate across the boundary. Every fallible call
SHALL catch all exceptions internally and report failure via a status code, storing a human-readable
message retrievable through a thread-local `rftrace_last_error()` accessor.

#### Scenario: A failing operation returns a status, not a crash
- **WHEN** a C API call fails (invalid handle, bad argument, or an internal C++ exception)
- **THEN** it SHALL return a non-OK status code and SHALL NOT throw, crash, or leak, and
  `rftrace_last_error()` SHALL return a message describing the failure

#### Scenario: Success path reports OK
- **WHEN** a C API call succeeds
- **THEN** it SHALL return the OK status and produce the documented outputs

### Requirement: Explicit handle ownership and memory safety
Handles (scene, simulator, result objects) SHALL be created by explicit `_create`/`_run` calls and
destroyed by matching `_destroy`/`_free` calls; the caller owns them. Result data SHALL be exposed
either copied into caller-provided buffers (with a size query) or via pointers whose lifetime is tied
to the owning result handle and documented. Destroying a handle SHALL free all memory it owns; no API
usage following the documented create/destroy protocol SHALL leak or double-free.

#### Scenario: Round-trip with no leaks
- **WHEN** a scene and simulator are created, a run produces a result, results are read, and all
  handles are destroyed in order
- **THEN** all memory SHALL be released (verified under a leak sanitizer) with no leak or double-free

#### Scenario: Result buffer size query
- **WHEN** a caller queries a result's element count and then reads into a buffer of that size
- **THEN** the API SHALL fill exactly that many elements and report truncation rather than overflow if
  the buffer is smaller

### Requirement: C API exercises scene, simulator, and results
The C API SHALL be sufficient to build a scene (materials, meshes from float vertex data,
transmitters, receivers), configure simulation settings via a POD struct mirroring
`SimulationSettings`, invoke `run`, `runCoverage`, and `runRoute`, and read their results
(per-receiver received power / path loss / delay spread / SINR, coverage grid arrays, route samples).
A C-language test SHALL link `librftrace_c` and exercise this end to end.

#### Scenario: End-to-end run from C
- **WHEN** the C test builds a simple scene, sets a transmitter and receiver, runs the simulation, and
  reads the received power for the receiver
- **THEN** the values SHALL match the equivalent C++ `Simulator` run for the same scene and settings

