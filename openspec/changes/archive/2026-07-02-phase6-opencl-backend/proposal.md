## Why

Phases 1-4 delivered a tested CPU RF-propagation core plus a validated Metal GPU backend, all
behind the `IBackend` ray-traversal contract, and Phase 5 authored a CUDA/OptiX backend (unverified
on this non-NVIDIA host). Phase 6 adds an **OpenCL GPU backend** that runs on a broad range of GPUs
(Apple, AMD, Intel, NVIDIA) without vendor-specific ray-tracing hardware. OpenCL has NO hardware
ray-tracing intrinsics, so this backend uploads its own **custom flat BVH buffers** to the device
and traverses them with an **iterative OpenCL C kernel** (an explicit fixed-size stack — OpenCL C is
C99 with no recursion). It accelerates ray traversal only, behind the existing `IBackend` contract,
mirroring exactly how the Metal backend (Phase 4) plugs in. RF physics, the scene model, the
simulator, and result formats do NOT change. The CPU backend remains the correctness reference;
OpenCL is validated against it with the same tolerance-based parity tests used for Metal.

Unlike the CUDA backend, this change is **VERIFIED on the development host**: macOS ships the Apple
OpenCL 1.2 framework, `find_package(OpenCL)` yields `OpenCL::OpenCL`, and `clGetDeviceIDs` finds one
GPU (Apple M2 Max, OpenCL 1.2). The backend is therefore built AND run here, and its parity tests
execute on the M2 Max GPU. The design is portable to any OpenCL 1.2+ GPU.

## What Changes

- Add an **OpenCL GPU backend** (`src/backends/opencl/opencl_backend.cpp` host + an embedded OpenCL C
  kernel string), compiled ONLY when `RFTRACE_ENABLE_OPENCL=ON` AND `find_package(OpenCL)` succeeds
  (defining `RFTRACE_HAVE_OPENCL=1`). It builds **custom flat BVH buffers** (nodes, triangle
  vertices, and the triangle-index permutation) from the scene triangles in float32 and uploads them
  to device buffers, then launches an **iterative traversal kernel** over a device buffer of rays,
  writing one hit per ray. There is **NO hardware ray tracing** — traversal is an explicit-stack BVH
  walk in OpenCL C (C99, no recursion). Occlusion uses an any-hit early-exit variant that stops at
  the first triangle intersected within `[tMin, tMax]`.
- Add **backend selection + fallback** following the exact Embree/Metal pattern: a factory
  `makeOpenclBackend()` declared in a header guarded by `RFTRACE_HAVE_OPENCL`, plus
  `openclDeviceAvailable()`. `makeBackend(Backend::OpenCL, ...)` calls the factory when compiled AND
  a device exists, catching runtime failures and falling back to CPU when `allowFallback` is true
  (else rethrow). `backendAvailable(Backend::OpenCL)` returns true only when `RFTRACE_HAVE_OPENCL` is
  defined AND an OpenCL platform/device exists at runtime.
- Override the additive **batched-query API** (`closestHitBatch`/`occludedBatch`, already added in
  Phase 4) with a single device dispatch: upload rays once, one `clEnqueueNDRangeKernel`, read hits
  back, converting double->float on upload and float->double on read-back. `Vec3`/`Hit` stay double
  on the C++ side.
- Add a small **additive flat-BVH accessor** to `BVH` (or build an equivalent flat BVH inside the
  OpenCL backend) exposing flat nodes / triangle vertices / the index permutation for upload. This
  accessor is additive and does NOT change the CPU BVH's behaviour.
- Add **CPU-vs-OpenCL parity tests** (built/run only under `RFTRACE_ENABLE_OPENCL`, guarded by
  `#if RFTRACE_HAVE_OPENCL`, `GTEST_SKIP()` when no device) comparing hit/miss exactly, the same
  triangle index for well-separated geometry, and `t` within tolerance (OpenCL is float32; CPU is
  double) — the same rule as the Metal parity tests: `|dt| <= max(1e-2 m, 1e-4*|t|)`.
- Implement the CMake `RFTRACE_ENABLE_OPENCL` option (currently a no-op): `find_package(OpenCL)`,
  link `OpenCL::OpenCL`, define `RFTRACE_HAVE_OPENCL=1`, and define `CL_SILENCE_DEPRECATION` and
  `CL_TARGET_OPENCL_VERSION=120` to pin/quiet the OpenCL 1.2 API on macOS. Add a `just opencl` recipe.

## Capabilities

### New Capabilities
- `opencl-backend`: the OpenCL GPU ray-traversal backend — custom flat-BVH device buffers, an
  iterative (explicit-stack) OpenCL C traversal kernel with an any-hit occlusion variant, host/device
  buffer layout, runtime kernel compilation via `clCreateProgramWithSource`, runtime availability
  detection, and the `makeOpenclBackend()` factory. Flag-gated with CPU fallback. VERIFIED on Apple
  OpenCL 1.2 (M2 Max) and portable to other OpenCL 1.2+ GPUs.

### Modified Capabilities
- `ray-simulation`: adds the CPU-vs-OpenCL parity requirement and the graceful-skip requirement for
  the OpenCL parity tests, reusing the already-defined batched-query API. No change to the existing
  batched-API requirement itself.

## Impact

- **Code:** new `src/backends/opencl/` (host `.cpp` + embedded OpenCL C kernel string) and a small
  guarded `include/rftrace/backends/opencl_backend.hpp` factory header; an additive flat-BVH accessor
  on `include/rftrace/bvh.hpp` / `src/core/bvh.cpp`; `src/backends/cpu_nanort/cpu_backend.cpp` gains
  `backendAvailable(OpenCL)`/`makeBackend(OpenCL)` wiring following the existing Metal/Embree pattern.
  CMake `RFTRACE_ENABLE_OPENCL` becomes a real implementation. New guarded test file under `tests/`.
  `justfile` gains an `opencl` recipe.
- **RF core:** unchanged. RF physics, scene, simulator, and result formats are untouched. OpenCL only
  accelerates ray traversal behind `IBackend`.
- **Precision:** OpenCL kernels work in float32 on the device; `Vec3`/`Hit` stay double on the C++
  side, converted to float only at the device-buffer boundary. Parity is tolerance-based.
- **Dependencies:** OpenCL 1.2+ (`find_package(OpenCL)` -> `OpenCL::OpenCL`). On macOS this is the
  Apple OpenCL framework; portable to AMD/Intel/NVIDIA OpenCL runtimes.
- **Verification status:** VERIFIED on this host — the default build (both new flags OFF) stays green
  (125 C++ tests), and an OpenCL-enabled build compiles and runs its parity tests on the Apple M2 Max
  GPU (OpenCL 1.2).
- **Out of scope:** the CUDA/OptiX backend (separate change), GPU-side RF math, BVH refit/motion, and
  any change to RF physics or result semantics.
