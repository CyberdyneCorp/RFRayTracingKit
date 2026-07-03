## 1. C API — header & ABI surface

- [x] 1.1 Author `include/rftrace/rftrace.h` (C-only): opaque handle typedefs (scene, simulator, result, coverage, route_result), `rftrace_status` enum, POD `rftrace_settings` / `rftrace_grid` / vector structs, and function declarations under `extern "C"`. Include `rftrace_version()` + ABI version macro.
- [x] 1.2 Add `rftrace_settings_default(rftrace_settings*)` and document append-only struct growth.
- [x] 1.3 Verify the header compiles as C (a `.c` TU that only `#include`s it).

## 2. C API — implementation

- [x] 2.1 `bindings/c/rftrace_c.cpp`: reinterpret-cast handle wrappers over `Scene`/`Simulator`; a no-throw macro + thread-local `rftrace_last_error()`.
- [x] 2.2 Scene building: create/destroy, add material, add mesh from float vertex arrays, add transmitter/receiver.
- [x] 2.3 Simulator: create from `rftrace_settings`, `run` / `runCoverage` (with `rftrace_grid`) / `runRoute`, returning result handles.
- [x] 2.4 Result reading: receiver count + per-receiver power/path-loss/delay/SINR via count-then-fill; coverage arrays; route samples; result `_free`.
- [x] 2.5 Map the settings/grid POD structs to/from the C++ types; `rftrace_settings_default` fills `SimulationSettings{}` defaults.

## 3. C API — build & tests

- [x] 3.1 CMake: `option(RFTRACE_ENABLE_C_API OFF)`; build `librftrace_c` (links core), install the header; guard so default build is unchanged.
- [x] 3.2 Add `tests/c_api_test.c` (compiled as C, linked to `librftrace_c`): end-to-end scene→run→read; assert values match the equivalent C++ run; drive error paths (bad handle/args → non-OK status, `rftrace_last_error` set); short-buffer truncation.
- [x] 3.3 Run the C test under ASan/LSan where available; assert no leaks/double-free.
- [x] 3.4 Add a `just c-api` recipe (configure + build + run the C test). Confirm the default C++ suite is unaffected.

## 4. Swift bindings (authored; unverified without a Swift toolchain)

- [x] 4.1 `bindings/swift/Package.swift`: a `CRFTrace` system-library target (module map over `rftrace/rftrace.h`, linking `librftrace_c`) + a `RFTrace` Swift target + a test target.
- [x] 4.2 Swift layer: `Scene`, `Simulator`, result value types; RAII via `deinit`; status→`throws RFTraceError`; no raw pointers in the public API.
- [x] 4.3 Swift package tests mirroring the C test (build a scene, run, read power) — runnable only where Swift + `librftrace_c` exist.
- [x] 4.4 Document the Swift package as UNVERIFIED on non-Swift hosts and how to build it (`swift build` against a built `librftrace_c`); keep it out of the CMake/CTest build.

## 5. Docs & archive

- [x] 5.1 `bindings/c/README.md` (C API usage + ownership/error rules) and `bindings/swift/README.md`.
- [x] 5.2 Update root README + `openspec/project.md` (move "Swift bindings + C API" from known gaps to done; note Swift is authored/unverified); run `openspec validate --strict` and archive the change.
