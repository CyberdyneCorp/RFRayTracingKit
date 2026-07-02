## Why

Phases 1-3 delivered a complete, tested CPU RF-propagation core (NanoRT-style BVH,
image-method and stochastic paths, coverage grids, JSON/CSV/GeoJSON/glTF export) plus a
Python API. The engine already isolates ray traversal behind the `IBackend` interface, but
the only implementation is the CPU BVH — every ray/occlusion query runs on one CPU. On
Apple Silicon (M2 Max, `supportsRaytracing = 1`) the GPU can traverse a hardware
acceleration structure orders of magnitude faster for the bulk ray queries that dominate
coverage grids and dense multipath launches. Phase 4 adds a **Metal GPU backend** that
accelerates ray traversal only, behind the existing `IBackend` contract, without touching
RF physics, the scene model, the simulator, or result formats. The CPU backend remains the
correctness reference; Metal is validated against it.

## What Changes

- Add an **additive batched-query API** to `IBackend` (two NON-pure virtual methods with
  default implementations that loop over the existing single-ray methods):
  `closestHitBatch(const std::vector<Ray>&) const -> std::vector<Hit>` and
  `occludedBatch(const std::vector<Ray>&) const -> std::vector<char>`. The CPU backend
  inherits the defaults; the Metal backend overrides them with a single GPU dispatch. This
  is backward compatible — no existing caller or backend changes behaviour.
- Add a **Metal GPU backend** (`src/backends/metal/metal_backend.mm`, Objective-C++,
  compiled only when `RFTRACE_ENABLE_METAL=ON` AND `APPLE`). It builds an
  `MTLAccelerationStructure` (primitive acceleration structure) from scene triangles
  (float3 vertices + index buffer, primitive index == our triangle index) and runs a
  `metal_raytracing` compute kernel (`raytracing::intersector<triangle_data>`,
  `acceleration_structure<>`) over a buffer of rays, writing hits. Occlusion uses
  `accept_any_intersection(true)`. The `.metal` kernel source is embedded as a string and
  compiled at RUNTIME via `newLibraryWithSource:` — no offline `.metallib` step.
- Add **backend selection + fallback**: a factory `makeMetalBackend()` declared in a small
  header guarded by `RFTRACE_HAVE_METAL`; `makeBackend(Backend::Metal, ...)` calls it when
  compiled, else falls back to CPU (preserving existing behaviour).
  `backendAvailable(Metal)` returns true only when `RFTRACE_HAVE_METAL` AND a Metal device
  actually exists at runtime.
- Add **CPU-vs-Metal parity tests** (built/run only under `RFTRACE_ENABLE_METAL`, guarded
  by `#if RFTRACE_HAVE_METAL`, `GTEST_SKIP()` when no GPU) comparing hit/miss exactly, the
  same triangle index for well-separated geometry, and `t` within tolerance (Metal is
  float32; CPU is double).
- Add a **`just metal` recipe** that configures `-DRFTRACE_ENABLE_METAL=ON` in `build-metal`
  and runs its ctest. Metal is NOT added to the default `ci` recipe; the default build and
  the existing 73 C++ tests plus Python tests stay green.

## Capabilities

### New Capabilities
- `metal-backend`: the Metal GPU ray-traversal backend — `MTLAccelerationStructure` build,
  runtime-compiled `metal_raytracing` intersect/occlusion kernels, host/kernel buffer
  layout, runtime availability detection, and the `makeMetalBackend()` factory.

### Modified Capabilities
- `ray-simulation`: adds the additive batched-query API requirement on the backend
  interface (default loop implementation, Metal single-dispatch override) and the
  CPU-vs-Metal parity + graceful-skip requirements.

## Impact

- **Code:** new `src/backends/metal/metal_backend.mm` and a small `metal_backend.hpp`
  factory header (both guarded); `include/rftrace/backend.hpp` gains the two additive
  batched virtual methods; `src/backends/cpu_nanort/cpu_backend.cpp` gains
  `backendAvailable(Metal)`/`makeBackend(Metal)` wiring following the existing Embree
  pattern. CMake `RFTRACE_ENABLE_METAL` becomes a real implementation
  (defines `RFTRACE_HAVE_METAL`, compiles the `.mm`, links `-framework Metal -framework
  Foundation`). New guarded test file under `tests/`. `justfile` gains `metal`.
- **RF core:** unchanged. RF physics, scene, simulator, and result formats are untouched
  except for the additive batched-query API. Metal only accelerates ray traversal behind
  `IBackend`.
- **Precision:** Metal works in float32 inside GPU buffers; `Vec3`/`Hit` stay double on the
  C++ side, converted to float only when filling Metal buffers. Parity is tolerance-based.
- **Dependencies:** Metal.framework + Foundation (system, macOS/Apple only); runtime shader
  compilation (no offline toolchain step).
- **Out of scope (later phases):** CUDA/OptiX and OpenCL backends, GPU-side RF math,
  motion/refit of the acceleration structure, and any change to RF physics or result
  semantics.
