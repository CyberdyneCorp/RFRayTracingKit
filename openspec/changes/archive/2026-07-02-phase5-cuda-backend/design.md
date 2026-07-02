## Context

The C++20 core (namespace `rftrace`) is complete through Phase 4 and isolates ray traversal behind
`IBackend` in `include/rftrace/backend.hpp`: `build(const std::vector<Triangle>&)`,
`closestHit(const Ray&) -> Hit`, `occluded(const Ray&) -> bool`, `kind()`, plus the additive batched
methods `closestHitBatch(const std::vector<Ray>&) const -> std::vector<Hit>` and
`occludedBatch(const std::vector<Ray>&) const -> std::vector<char>` (default implementations loop the
single-ray methods). `Vec3` is `Eigen::Vector3d` (double); `Triangle{v0,v1,v2}`,
`Ray{origin,direction,tMin,tMax}`, `Hit{valid,t,u,v,triangle}`. The CPU NanoRT-style BVH
(`src/backends/cpu_nanort/cpu_backend.cpp`) is the reference and defines the
factory/availability free functions, gating Embree behind `RFTRACE_HAVE_EMBREE` and Metal behind
`RFTRACE_HAVE_METAL`. Phase 4's Metal backend (`src/backends/metal/metal_backend.mm`) is the exact
structural template this change mirrors on NVIDIA hardware.

**Environment constraint (critical):** the development host is an Apple M2 Max with NO `nvcc`, NO
CUDA Toolkit, and NO OptiX SDK (`OptiX_INSTALL_DIR` unset). Therefore this backend CANNOT be compiled
or run here. It is authored to the CUDA/OptiX API, gated so the default build is completely
unaffected, and left **UNVERIFIED — to be validated on NVIDIA hardware.** The overriding project
constraint holds: **keep RF physics INDEPENDENT of the ray backend.** CUDA only accelerates ray
traversal behind `IBackend`; RF math, scene, simulator, and result formats do not change.

## Goals / Non-Goals

**Goals:**
- A CUDA/OptiX GPU backend implementing `IBackend` using OptiX hardware ray tracing (RT cores) with
  CUDA batched intersect/occlusion kernels.
- Backend selection with graceful CPU fallback and honest runtime availability detection, mirroring
  the Metal/Embree pattern exactly.
- Tolerance-based CPU-vs-CUDA parity tests and graceful skip when no NVIDIA GPU is present.
- The default build (both `RFTRACE_ENABLE_CUDA` and `RFTRACE_ENABLE_OPENCL` OFF) and the existing
  125 C++ tests stay green — the primary regression guard.

**Non-Goals:**
- The OpenCL backend (separate change); GPU-side RF math; AS refit/motion.
- Any change to RF physics, scene, simulator logic, or result formats.
- Compiling or running CUDA on this host (impossible — no toolkit). No claim of a green CUDA build
  is made here.

## Resolved Decisions

These are decided for Phase 5 (CUDA) and are NOT open questions.

### D1. OptiX hardware ray tracing + CUDA kernels
`build()` creates an OptiX geometry acceleration structure (GAS): float32 vertex and `unsigned int`
index device buffers uploaded via `cudaMalloc`/`cudaMemcpy`, described by an
`OptixBuildInput` (triangle array), built with `optixAccelBuild` (with compaction), yielding an
`OptixTraversableHandle`. **Primitive index == our triangle index**, so an OptiX hit's
`optixGetPrimitiveIndex()` maps directly back to the `Triangle` at that index. The device program
(raygen + closesthit + miss, plus anyhit/miss for occlusion) is launched with `optixLaunch` over a
device buffer of rays, writing a hit record per ray. Closest-hit uses `OPTIX_RAY_FLAG_NONE`;
occlusion uses `OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT` (with `OPTIX_RAY_FLAG_DISABLE_ANYHIT` where
appropriate) so traversal stops at the first hit within `[tmin, tmax]`.

### D2. CUDA/C++ implementation, guarded and isolated
The host side is `src/backends/cuda/cuda_backend.cpp` (or `.cu`), compiled ONLY when
`RFTRACE_ENABLE_CUDA=ON` AND `CUDAToolkit` + OptiX are found. When enabled, CMake defines
`RFTRACE_HAVE_CUDA=1`. The `.cpp` exposes a factory `std::unique_ptr<IBackend> makeCudaBackend()`
and `bool cudaDeviceAvailable()`, both declared in `include/rftrace/backends/cuda_backend.hpp` guarded
by `#if RFTRACE_HAVE_CUDA` — analogous to `metal_backend.hpp`. The device program
(`src/backends/cuda/cuda_programs.cu`) is compiled to PTX (via `nvcc`/CUDA CMake with
`CUDA_PTX_COMPILATION`, or embedded), loaded at runtime through `optixModuleCreate`. `makeBackend`
and `backendAvailable` wiring in `cpu_backend.cpp` follows the Metal branch byte-for-byte in shape.

### D3. Backend selection, availability, and CPU fallback
`backendAvailable(Backend::CUDA)` returns true only when `RFTRACE_HAVE_CUDA` is defined AND a CUDA
device exists at runtime (`cudaDeviceAvailable()`: `cudaGetDeviceCount() > 0` and OptiX init
succeeds). `makeBackend(Backend::CUDA, allowFallback)` calls `makeCudaBackend()` when compiled and a
device exists, wrapped in try/catch: on runtime failure (driver/OptiX init, PTX load, AS build) it
falls back to CPU when `allowFallback` is true, else rethrows. When not compiled, it falls back to
CPU (or throws when `!allowFallback`), preserving the existing behaviour. This mirrors the Metal
branch exactly.

### D4. Precision: float32 GPU, double C++, tolerance parity
OptiX/CUDA traversal is float32; the CPU reference is double. The device acceleration structure and
kernels work in float. `Vec3`/`Hit` stay double on the C++ side; values convert to float only when
filling device buffers and convert back to double when reading hits out. Parity tests compare
hit/miss EXACTLY, require the SAME triangle index for well-separated geometry, and compare `t` within
tolerance `|dt| <= max(1e-2 m, 1e-4*|t|)` — identical to the Metal parity rule.

### D5. Buffer layout (host struct matches device struct)
Define matching POD structs on host and device. To avoid CUDA `float3` alignment surprises, use
explicit scalars or packed layouts:

```
Ray  { float ox, oy, oz;  float dx, dy, dz;  float tmin, tmax; }
Hit  { float t;  unsigned int prim;  float u, v;  unsigned int valid; }
```

Ray/hit buffers live in device memory (`cudaMalloc`); rays are uploaded once per batch and hits are
copied back once per batch. Static-assert host struct sizes so host and device agree.

### D6. Batched single-launch dispatch
Override `closestHitBatch`/`occludedBatch` with a single `optixLaunch`: upload the whole ray buffer,
launch a 1-D grid of `rays.size()` threads (one ray per launch index), read the hit buffer back,
convert float->double. Single-ray `closestHit`/`occluded` are implemented as a one-element batch.
`kind()` returns `Backend::CUDA`. Rationale: per-ray host<->device round-trips are pointless; batching
is where the GPU pays off — the same rationale as Metal.

### D7. Tests: build only under CUDA, skip at runtime without a GPU
The parity/behaviour test file (`tests/test_cuda_parity.cpp`) builds and runs ONLY under
`RFTRACE_ENABLE_CUDA`; it is guarded with `#if RFTRACE_HAVE_CUDA` and at runtime calls `GTEST_SKIP()`
when `backendAvailable(Backend::CUDA)` is false. The default build (no CUDA flag) is unaffected: the
existing 125 C++ tests keep passing. Mirrors the Metal parity test structure.

### D8. `just cuda` recipe, not in default CI
Add a `just cuda` recipe that configures with `-DRFTRACE_ENABLE_CUDA=ON` (and `OptiX_INSTALL_DIR`)
into `build-cuda` and runs its ctest. CUDA is NOT added to the default `ci` recipe. On this
non-NVIDIA host the recipe will FAIL to configure (no `CUDAToolkit`/OptiX) — that is expected and
acceptable per the task.

## Risks / Trade-offs

- **UNVERIFIED build** — the CUDA backend cannot be compiled or run on this host. Mitigation: strict
  gating means the default build is untouched (125 tests green); the code follows the working Metal
  backend structure and the documented OptiX API; validation is deferred to NVIDIA hardware and
  called out explicitly in the proposal, design, and tasks.
- **float32 vs double parity** — device traversal in float can disagree with the double CPU reference
  near grazing angles / shared edges. Mitigation: parity asserts require well-separated geometry for
  exact triangle-index equality and use the `max(1e-2, 1e-4*|t|)` tolerance on `t`.
- **OptiX SDK / driver mismatch** — OptiX ABI and CUDA driver versions must align. Mitigation: guard
  on `OptiX_INSTALL_DIR`, check every OptiX/CUDA return code, surface a clear error, and treat the
  backend as unavailable (fall back to CPU) rather than crashing.
- **PTX compilation / module load failure** — `optixModuleCreate` can fail on an unexpected toolchain.
  Mitigation: check the OptiX log/return, treat the backend as unavailable.
- **No GPU present** — `backendAvailable(CUDA)` gates on an actual device + successful OptiX init;
  `makeBackend` falls back to CPU; tests `GTEST_SKIP()`.

## Migration Plan

Additive and backward compatible. Default builds ignore CUDA entirely (`RFTRACE_HAVE_CUDA` undefined;
no CUDA source compiled). Enabling `-DRFTRACE_ENABLE_CUDA=ON` with a valid `CUDAToolkit`/OptiX
compiles the backend and its tests on NVIDIA hardware. No data migration; no changes to serialized
result formats.

## Open Questions

None. All Phase 5 (CUDA) decisions (D1-D8) are resolved above. The only outstanding item is
**hardware validation on an NVIDIA + OptiX machine**, which cannot be performed on the current host.
