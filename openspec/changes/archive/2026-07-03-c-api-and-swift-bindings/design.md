## Context

The core is modern C++ (Eigen, exceptions, RAII, `std::vector`/`std::string`). Swift and most FFIs
bind to C, not C++. A stable `extern "C"` ABI is therefore the enabling layer. The public surface to
wrap: `Scene` (materials, `addMesh`, transmitters, receivers), `SimulationSettings`, `Simulator`
(`run`/`runCoverage`/`runRoute`), and the result types (`RFResult`/`ReceiverResult`,
`CoverageResult`, `RouteResult`). Constraint: **no core changes**; the C layer only wraps. This host
has a C/C++ toolchain (verifiable) but **no Swift toolchain** (Swift authored-but-unverified).

## Goals / Non-Goals

**Goals:**
- A minimal, stable, no-throw C ABI sufficient to build a scene, run all three simulation modes, and
  read results; with explicit ownership, status/error handling, and versioning.
- A C test that links the C lib and matches the C++ result (buildable here, sanitizer-clean).
- An idiomatic SwiftPM package over that C API (throwing, value types, RAII), authored to spec.

**Non-Goals:**
- Not exposing the entire C++ surface (no importers/exporters/advanced-RF knobs beyond the settings
  struct in v1 — additive later); not binding the backends directly (backend chosen via settings).
- No core refactor; no new heavy dependencies; not wiring Swift into CMake/CTest.
- Not an ABI-stability guarantee across major versions beyond the reported ABI version.

## Decisions

### D1 — Opaque handles + POD structs, no C++ in the header
`rftrace/rftrace.h` uses only C: opaque `typedef struct rftrace_scene rftrace_scene;` (and
`rftrace_simulator`, `rftrace_result`, `rftrace_coverage`, `rftrace_route_result`), POD structs for
settings/grid/vectors (plain `double`/`int`), a `rftrace_status` enum, and function decls guarded by
`#ifdef __cplusplus extern "C" {`. The `.cpp` reinterpret-casts handles to the C++ objects. *Why:* the
header must compile as C for Swift's module map and any C consumer.

### D2 — No-throw boundary via a translation macro
Every entry point wraps its body in `try { ... return RFTRACE_OK; } catch (const std::exception& e)
{ set_last_error(e.what()); return RFTRACE_ERROR; } catch (...) { ... }`. A thread-local
`std::string` holds the last error; `rftrace_last_error()` returns `.c_str()`. A small macro keeps the
wrappers uniform and low-complexity. *Why:* an exception crossing into C is UB; this makes the ABI
safe and diagnosable. *Alternative:* per-function try/catch by hand — rejected (verbose, error-prone).

### D3 — Ownership: explicit create/destroy; results copied by size-query
Handles are heap C++ objects owned by the caller (`*_create` / `*_destroy`). Variable-length results
(receiver arrays, coverage grids, route samples) use the count-then-fill pattern:
`rftrace_result_receiver_count(r, &n)` then `rftrace_result_receiver_powers(r, buf, n, &written)`,
copying into caller memory (truncate-and-report if `n` is short). Scalar/opaque sub-results are read by
index. *Why:* copying out is the simplest safe lifetime model for FFI; no dangling pointers into freed
C++ vectors. *Alternative:* expose internal pointers — allowed only where lifetime is tied to a live
result handle and documented, but prefer copy for v1.

### D4 — Settings/grid as C POD mirrors
`rftrace_settings` mirrors the additive fields of `SimulationSettings` (backend enum, mode enum,
maxReflections, threadCount, frequency/power via tx, flags…). Provide
`rftrace_settings_default(&s)` to fill defaults so ABI growth stays source-compatible (new fields
appended; callers memset+default). `rftrace_grid` mirrors `CoverageGrid`. *Why:* a defaulting
initializer decouples callers from field additions.

### D5 — Build + packaging
CMake `RFTRACE_ENABLE_C_API` (default OFF) builds `librftrace_c` (links the core), installs the
header, and adds a C test (`tests/c_api_test.c`) compiled with a C compiler and linked against the C
lib, run under ASan/LSan where available. A `just c-api` recipe configures+builds+tests it. The Swift
package under `bindings/swift/` is a standalone SwiftPM tree: a `CRFTrace` system-library target (module
map over `rftrace.h`) + a `RFTrace` Swift target; it points at a built `librftrace_c`. It is NOT added
to CMake/CTest.

### D6 — Swift layer shape
`RFTrace` wraps handles in Swift classes with `deinit { rftrace_*_destroy(h) }`; converts status
codes to a `throws RFTraceError(message:)`; presents `Scene`, `Simulator`, and `struct`-based results
(`[Double]` arrays). No raw pointers in the public API. Authored to the C header; compiled only where
Swift exists.

## Risks / Trade-offs

- **[Exception crossing the C boundary → UB/crash]** → D2 no-throw macro on every entry point; the C
  test drives error paths (bad handle, bad args) and asserts status codes.
- **[Memory leak / double-free at the FFI seam]** → D3 explicit ownership + copy-out; C test runs
  under LSan/ASan and asserts no leaks.
- **[Result buffer overflow]** → count-then-fill with truncation reporting (D3), tested with a short
  buffer.
- **[ABI drift as settings grow]** → `rftrace_settings_default` + append-only struct growth + a
  reported ABI version (D4/D1).
- **[Swift layer unverified here]** → authored strictly to the C header and documented UNVERIFIED
  (mirrors the pre-hardware CUDA backend); validated when a Swift toolchain is available; kept out of
  the default build so it cannot break CI.

## Migration Plan

- PR 1: C API (header + impl + CMake flag + C test + `just c-api`) — fully verifiable here.
- PR 2: Swift package (`bindings/swift/`) authored to the C API — unverified; marked as such; inert in
  the default build.
- Update README + project.md (move Swift/C API from known gaps to done, noting Swift is unverified).

## Open Questions

- Which subset of advanced-RF settings to expose in `rftrace_settings` v1 vs defer (start with the
  core + threadCount + common Phase-7 flags; grow additively).
- Copy-out everywhere vs lifetime-tied pointers for large coverage arrays (start copy-out; revisit if
  a zero-copy accessor is needed for very large grids).
- Swift error type granularity (single `RFTraceError` with message vs a status-code enum) — start
  simple with a message-carrying error.
