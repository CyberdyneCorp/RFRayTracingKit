## Context

The C++20 core (namespace `rftrace`) is complete through Phase 4 and isolates ray traversal behind
`IBackend` in `include/rftrace/backend.hpp`: `build(const std::vector<Triangle>&)`,
`closestHit(const Ray&) -> Hit`, `occluded(const Ray&) -> bool`, `kind()`, plus the additive batched
methods `closestHitBatch(const std::vector<Ray>&) const -> std::vector<Hit>` and
`occludedBatch(const std::vector<Ray>&) const -> std::vector<char>` (default implementations loop the
single-ray methods). `Vec3` is `Eigen::Vector3d` (double); `Triangle{v0,v1,v2}`,
`Ray{origin,direction,tMin,tMax}`, `Hit{valid,t,u,v,triangle}`. The CPU NanoRT-style BVH
(`include/rftrace/bvh.hpp`, `src/core/bvh.cpp`, median split, `Node{AABB box; int32 left; uint32
start,count}`, an `indices_` permutation into `triangles_`) is the reference and
`src/backends/cpu_nanort/cpu_backend.cpp` defines the factory/availability free functions, gating
Embree behind `RFTRACE_HAVE_EMBREE` and Metal behind `RFTRACE_HAVE_METAL`. Phase 4's Metal backend
(`src/backends/metal/metal_backend.mm`) is the exact structural template this change mirrors.

**Environment (verified):** the development host is an Apple M2 Max on macOS with the Apple OpenCL
1.2 framework at `/System/Library/Frameworks/OpenCL.framework`. `find_package(OpenCL)` succeeds and
yields `OpenCL::OpenCL`; `clGetDeviceIDs` finds one GPU (Apple M2 Max, OpenCL 1.2). Therefore this
backend is **built AND run here** — unlike the CUDA backend, it is not unverified. OpenCL exposes NO
hardware ray-tracing intrinsics, so this backend supplies its OWN acceleration structure (a custom
flat BVH) and traverses it in an OpenCL C kernel. The overriding project constraint holds: **keep RF
physics INDEPENDENT of the ray backend.** OpenCL only accelerates ray traversal behind `IBackend`; RF
math, scene, simulator, and result formats do not change.

## Goals / Non-Goals

**Goals:**
- An OpenCL GPU backend implementing `IBackend` via a custom flat BVH uploaded to the device and an
  iterative (explicit-stack) OpenCL C traversal kernel — NO hardware ray tracing.
- Backend selection with graceful CPU fallback and honest runtime availability detection, mirroring
  the Metal/Embree pattern exactly.
- Tolerance-based CPU-vs-OpenCL parity tests, run on the Apple M2 Max GPU, and graceful skip when no
  OpenCL device is present.
- The default build (both `RFTRACE_ENABLE_OPENCL` and `RFTRACE_ENABLE_CUDA` OFF) and the existing 125
  C++ tests stay green — the primary regression guard.

**Non-Goals:**
- The CUDA/OptiX backend (separate change); GPU-side RF math; BVH refit/motion.
- Any change to RF physics, scene, simulator logic, or result formats.
- Any hardware ray-tracing path — OpenCL 1.2 has none; traversal is a software BVH walk.

## Resolved Decisions

These are decided for Phase 6 (OpenCL) and are NOT open questions.

### D1. Custom flat BVH + iterative OpenCL C traversal (no hardware ray tracing)
OpenCL 1.2 has no ray-tracing intrinsics, so `build()` constructs a **flat BVH** (float32 node
buffer, triangle-vertex buffer, and a triangle-index permutation buffer) and uploads it to device
memory. The kernel is plain **OpenCL C (C99)** and traverses the BVH **iteratively** with an
explicit fixed-size stack (NO recursion — OpenCL C forbids it): pop a node, test its AABB against the
ray, and either descend to children or run a Moller-Trumbore test over the leaf triangles. A
closest-hit kernel keeps the nearest `t`; an any-hit occlusion kernel early-exits at the first
triangle intersected within `[tmin, tmax]`. **Primitive index == our triangle index** via the index
permutation, so a hit resolves directly back to the `Triangle`.

### D2. OpenCL/C++ implementation, guarded and isolated
The host side is `src/backends/opencl/opencl_backend.cpp`, compiled ONLY when
`RFTRACE_ENABLE_OPENCL=ON` AND `find_package(OpenCL)` succeeds. When enabled, CMake defines
`RFTRACE_HAVE_OPENCL=1` and also `CL_SILENCE_DEPRECATION` + `CL_TARGET_OPENCL_VERSION=120` to
pin/quiet Apple's deprecated OpenCL 1.2 API. The `.cpp` exposes a factory
`std::unique_ptr<IBackend> makeOpenclBackend()` and `bool openclDeviceAvailable()`, both declared in
`include/rftrace/backends/opencl_backend.hpp` guarded by `#if RFTRACE_HAVE_OPENCL` — analogous to
`metal_backend.hpp`. The kernel source is an embedded C string compiled at RUNTIME via
`clCreateProgramWithSource` + `clBuildProgram`; a build failure captures the build log, is surfaced
as a clear error, and marks the backend unavailable rather than crashing. `makeBackend` and
`backendAvailable` wiring in `cpu_backend.cpp` follows the Metal branch byte-for-byte in shape.

### D3. Backend selection, availability, and CPU fallback
`backendAvailable(Backend::OpenCL)` returns true only when `RFTRACE_HAVE_OPENCL` is defined AND an
OpenCL platform with a usable device exists at runtime (`openclDeviceAvailable()`: `clGetPlatformIDs`
> 0 and `clGetDeviceIDs` finds a GPU/device). `makeBackend(Backend::OpenCL, allowFallback)` calls
`makeOpenclBackend()` when compiled and a device exists, wrapped in try/catch: on runtime failure
(no device, context/queue creation, program build, buffer alloc) it falls back to CPU when
`allowFallback` is true, else rethrows. When not compiled, it falls back to CPU (or throws when
`!allowFallback`), preserving existing behaviour. This mirrors the Metal branch exactly.

### D4. Precision: float32 GPU, double C++, tolerance parity
OpenCL traversal is float32; the CPU reference is double. The device flat BVH and kernels work in
float. `Vec3`/`Hit` stay double on the C++ side; values convert to float only when filling device
buffers and convert back to double when reading hits out. Parity tests compare hit/miss EXACTLY,
require the SAME triangle index for well-separated geometry, and compare `t` within tolerance
`|dt| <= max(1e-2 m, 1e-4*|t|)` — identical to the Metal parity rule.

### D5. Flat-BVH accessor and buffer layout (host struct matches device struct)
Expose the CPU BVH's structure to the backend via an ADDITIVE public accessor (flat nodes, triangle
vertices, index permutation) — additive so the CPU BVH's behaviour is unchanged — or build an
equivalent flat BVH inside the OpenCL backend. Define matching POD structs on host and device using
explicit float scalars to avoid `float3`/`float4` alignment surprises:

```
Node { float bmin[3]; float bmax[3]; int left; uint start; uint count; }
Ray  { float ox, oy, oz;  float dx, dy, dz;  float tmin, tmax; }
Hit  { float t;  uint prim;  float u, v;  uint valid; }
```

BVH, ray, and hit buffers live in device memory (`clCreateBuffer`); the flat BVH is uploaded once at
`build()`, rays are uploaded once per batch, and hits are copied back once per batch. Static-assert
host struct sizes so host and device agree.

### D6. Batched single-dispatch
Override `closestHitBatch`/`occludedBatch` with a single `clEnqueueNDRangeKernel`: upload the whole
ray buffer, launch a 1-D NDRange of `rays.size()` work-items (one ray per global id), read the hit
buffer back, convert float->double. Single-ray `closestHit`/`occluded` are implemented as a
one-element batch. `kind()` returns `Backend::OpenCL`. Rationale: per-ray host<->device round-trips
are pointless; batching is where the GPU pays off — the same rationale as Metal.

### D7. Tests: build only under OpenCL, skip at runtime without a device
The parity/behaviour test file (`tests/test_opencl_parity.cpp`) builds and runs ONLY under
`RFTRACE_ENABLE_OPENCL`; it is guarded with `#if RFTRACE_HAVE_OPENCL` and at runtime calls
`GTEST_SKIP()` when `backendAvailable(Backend::OpenCL)` is false. On this host a device IS present, so
the tests actually execute on the M2 Max GPU. The default build (no OpenCL flag) is unaffected: the
existing 125 C++ tests keep passing. Mirrors the Metal parity test structure.

### D8. `just opencl` recipe, not in default CI
Add a `just opencl` recipe that configures with `-DRFTRACE_ENABLE_OPENCL=ON` into `build-opencl` and
runs its ctest. OpenCL is NOT added to the default `ci` recipe (which stays the flags-OFF regression
guard). On this host the recipe configures, builds, and runs parity tests on the GPU.

## Risks / Trade-offs

- **No hardware ray tracing** — a software BVH walk is slower than RT-core traversal (Metal/OptiX).
  Accepted: OpenCL 1.2's value is portability, not peak throughput; correctness and portability are
  the goals here.
- **float32 vs double parity** — device traversal in float can disagree with the double CPU reference
  near grazing angles / shared edges. Mitigation: parity asserts require well-separated geometry for
  exact triangle-index equality and use the `max(1e-2, 1e-4*|t|)` tolerance on `t`.
- **Explicit-stack depth** — a fixed-size traversal stack could overflow on a pathologically deep
  BVH. Mitigation: the CPU BVH uses median split with a small leaf size, so depth is `O(log N)`; the
  stack is sized with generous headroom and the build asserts depth fits.
- **Deprecated Apple OpenCL API** — macOS marks OpenCL deprecated. Mitigation: `CL_SILENCE_DEPRECATION`
  + `CL_TARGET_OPENCL_VERSION=120` pin and quiet the API; the code targets OpenCL 1.2 only.
- **Kernel build failure / no device** — `clBuildProgram` can fail on an unexpected runtime, or no
  device may exist. Mitigation: capture the build log, check every OpenCL return code, treat the
  backend as unavailable (fall back to CPU), and `GTEST_SKIP()` in tests.

## Migration Plan

Additive and backward compatible. Default builds ignore OpenCL entirely (`RFTRACE_HAVE_OPENCL`
undefined; no OpenCL source compiled). Enabling `-DRFTRACE_ENABLE_OPENCL=ON` where `find_package(OpenCL)`
succeeds compiles the backend and its tests, which run on any OpenCL 1.2+ GPU (verified on the Apple
M2 Max). The additive flat-BVH accessor does not alter existing CPU BVH behaviour. No data migration;
no changes to serialized result formats.

## Open Questions

None. All Phase 6 (OpenCL) decisions (D1-D8) are resolved above and are verified on the Apple
OpenCL 1.2 (M2 Max) host; the design is portable to other OpenCL 1.2+ GPUs.
