## Why

Phases 1-4 delivered a tested CPU RF-propagation core plus a validated Metal GPU backend, all
behind the `IBackend` ray-traversal contract. On NVIDIA hardware the fastest available ray
traversal is **OptiX hardware ray tracing** (RT cores) driven by **CUDA** kernels. Phase 5 adds a
**CUDA/OptiX GPU backend** that accelerates ray traversal only, behind the existing `IBackend`
contract, mirroring exactly how the Metal backend (Phase 4) plugs in. RF physics, the scene model,
the simulator, and result formats do NOT change. The CPU backend remains the correctness reference;
CUDA is validated against it with the same tolerance-based parity tests used for Metal.

This change is **explicitly UNVERIFIED on non-NVIDIA hosts**. The development environment (Apple
M2 Max) has no `nvcc`, no CUDA toolkit, and no OptiX SDK, so the CUDA backend CANNOT be compiled or
run here. The code is authored carefully to the OptiX/CUDA API and is gated so that it has zero
effect on the default build; it is to be validated on NVIDIA hardware. Parity tests are
compile-guarded (`#if RFTRACE_HAVE_CUDA`) and `GTEST_SKIP()` when no device is present.

## What Changes

- Add a **CUDA/OptiX GPU backend** (`src/backends/cuda/cuda_backend.cu` / `.cpp` host + a
  device program compiled to PTX), compiled ONLY when `RFTRACE_ENABLE_CUDA=ON` AND both
  `CUDAToolkit` and OptiX (`OptiX_INSTALL_DIR`) are found (defining `RFTRACE_HAVE_CUDA=1`). It
  builds an **OptiX acceleration structure** (`OptixTraversableHandle`, a GAS) from scene
  triangles (float32 vertex + index buffers, primitive index == our `Triangle` index) and launches
  CUDA/OptiX kernels over a device buffer of rays, writing hits. Closest-hit uses the default
  `OPTIX_RAY_FLAG_NONE`; occlusion uses `OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT` and an anyhit/miss
  pair so traversal stops at the first hit.
- Add **backend selection + fallback** following the exact Embree/Metal pattern: a factory
  `makeCudaBackend()` declared in a header guarded by `RFTRACE_HAVE_CUDA`, plus
  `cudaDeviceAvailable()`. `makeBackend(Backend::CUDA, ...)` calls the factory when compiled AND a
  device exists, catching runtime failures and falling back to CPU when `allowFallback` is true
  (else rethrow). `backendAvailable(Backend::CUDA)` returns true only when `RFTRACE_HAVE_CUDA` is
  defined AND a CUDA device exists at runtime.
- Override the additive **batched-query API** (`closestHitBatch`/`occludedBatch`, already added in
  Phase 4) with a single device launch: upload rays once, one `optixLaunch`, read hits back,
  converting double->float on upload and float->double on read-back. `Vec3`/`Hit` stay double on
  the C++ side.
- Add **CPU-vs-CUDA parity tests** (built/run only under `RFTRACE_ENABLE_CUDA`, guarded by
  `#if RFTRACE_HAVE_CUDA`, `GTEST_SKIP()` when no GPU) comparing hit/miss exactly, the same triangle
  index for well-separated geometry, and `t` within tolerance (CUDA is float32; CPU is double) —
  the same rule as the Metal parity tests: `|dt| <= max(1e-2 m, 1e-4*|t|)`.
- Implement the CMake `RFTRACE_ENABLE_CUDA` option (currently a no-op): `enable_language(CUDA)`,
  `find_package(CUDAToolkit REQUIRED)`, locate OptiX via `OptiX_INSTALL_DIR`, compile the device
  program to PTX, and define `RFTRACE_HAVE_CUDA=1`. Add a `just cuda` recipe (which will fail to
  configure on this non-NVIDIA host — that is expected and acceptable).

## Capabilities

### New Capabilities
- `cuda-backend`: the CUDA/OptiX GPU ray-traversal backend — OptiX acceleration-structure build,
  CUDA/OptiX batched closest-hit/occlusion kernels, host/device buffer layout, PTX device-program
  compilation, runtime availability detection, and the `makeCudaBackend()` factory. Flag-gated and
  UNVERIFIED on non-NVIDIA hosts.

### Modified Capabilities
- `ray-simulation`: adds the CPU-vs-CUDA parity requirement and the graceful-skip requirement for
  the CUDA parity tests, reusing the already-defined batched-query API. No change to the existing
  batched-API requirement itself.

## Impact

- **Code:** new `src/backends/cuda/` (host `.cpp`/`.cu` + device program) and a small guarded
  `include/rftrace/backends/cuda_backend.hpp` factory header; `src/backends/cpu_nanort/cpu_backend.cpp`
  gains `backendAvailable(CUDA)`/`makeBackend(CUDA)` wiring following the existing Metal/Embree
  pattern. CMake `RFTRACE_ENABLE_CUDA` becomes a real implementation. New guarded test file under
  `tests/`. `justfile` gains a `cuda` recipe.
- **RF core:** unchanged. RF physics, scene, simulator, and result formats are untouched. CUDA only
  accelerates ray traversal behind `IBackend`.
- **Precision:** CUDA/OptiX works in float32 on the device; `Vec3`/`Hit` stay double on the C++
  side, converted to float only at the device-buffer boundary. Parity is tolerance-based.
- **Dependencies:** CUDA Toolkit (`CUDAToolkit`) + OptiX SDK (`OptiX_INSTALL_DIR`), NVIDIA-only.
- **Verification status:** UNVERIFIED on this host (no NVIDIA GPU / no CUDA / no OptiX). The default
  build (both new flags OFF) MUST stay green (125 C++ tests). To be validated on NVIDIA hardware.
- **Out of scope:** the OpenCL backend (separate change), GPU-side RF math, AS refit/motion, and any
  change to RF physics or result semantics.
