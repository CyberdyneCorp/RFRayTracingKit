## Context

The C++20 core (namespace `rftrace`) is complete through Phase 3 and isolates ray traversal
behind `IBackend` in `include/rftrace/backend.hpp`: `build(const std::vector<Triangle>&)`,
`closestHit(const Ray&) -> Hit`, `occluded(const Ray&) -> bool`, `kind()`, plus free
functions `toString`/`backendFromString`/`backendAvailable`/`makeBackend`. `Vec3` is
`Eigen::Vector3d` (double); `Triangle{v0,v1,v2}`, `Ray{origin,direction,tMin,tMax}`,
`Hit{valid,t,u,v,triangle}`. The only backend is the CPU NanoRT-style BVH
(`src/backends/cpu_nanort/cpu_backend.cpp`), which also defines the factory/availability
free functions and gates Embree behind `RFTRACE_HAVE_EMBREE`.

The target environment is macOS on Apple M2 Max with `supportsRaytracing = 1`, full Xcode +
Metal.framework. Runtime Metal shader compilation via `[device newLibraryWithSource:...]`
is verified working; compute dispatch and shared buffers work headless. The overriding
constraint from the project charter and the phase rules: **keep RF physics INDEPENDENT of
the ray backend.** Metal only accelerates ray traversal behind `IBackend`; RF math, scene,
simulator, and result formats do not change (except the additive batched-query API).

## Goals / Non-Goals

**Goals:**
- A Metal GPU backend implementing `IBackend` using hardware ray tracing
  (`MTLAccelerationStructure` + `metal_raytracing` kernels).
- An additive, backward-compatible batched-query API on `IBackend` where Metal pays off.
- Backend selection with graceful CPU fallback and honest runtime availability detection.
- Tolerance-based CPU-vs-Metal parity tests and graceful skip when no GPU is present.
- The default (non-Metal) build and the existing 73 C++ tests + Python tests stay green.

**Non-Goals:**
- CUDA/OptiX or OpenCL backends; GPU-side RF math; AS refit/motion.
- Any change to RF physics, scene, simulator logic, or result formats beyond the additive
  batched API.
- An offline `.metallib` build step (kernels compile at runtime).

## Resolved Decisions

These are decided for Phase 4 and are NOT open questions.

### D1. Additive batched-query API on `IBackend`
Add two NON-pure virtual methods with default implementations that loop the single-ray
methods:

```cpp
virtual std::vector<Hit> closestHitBatch(const std::vector<Ray>& rays) const;   // default: loop closestHit
virtual std::vector<char> occludedBatch(const std::vector<Ray>& rays) const;    // default: loop occluded
```

`occludedBatch` returns `std::vector<char>` (NOT `std::vector<bool>`) so results are a
contiguous byte buffer usable directly with GPU shared memory. The CPU backend inherits the
defaults unchanged; the Metal backend OVERRIDES both with a single GPU dispatch. Rationale:
per-ray host<->GPU round-trips are pointless; batching is where the GPU pays off. This is
purely additive — existing callers and the CPU backend are unaffected.

### D2. Hardware ray tracing via `MTLAccelerationStructure` + `metal_raytracing`
`build()` creates a **primitive** acceleration structure from the scene triangles: a
`float3` (packed) vertex buffer and a `uint` index buffer, sized/allocated via
`MTLAccelerationStructureTriangleGeometryDescriptor`, built with an
`MTLAccelerationStructureCommandEncoder` (scratch buffer + optional compaction).
**Primitive index == our triangle index**, so a GPU hit maps directly back to the
`Triangle` at that index. The compute kernel uses `metal_raytracing`
(`raytracing::intersector<triangle_data>` over `acceleration_structure<>`) to intersect a
buffer of rays and write hits. Occlusion uses `intersector.accept_any_intersection(true)`
so the traversal stops at the first hit within `[tmin, tmax]`.

### D3. Objective-C++ implementation, guarded and isolated
The implementation is `src/backends/metal/metal_backend.mm` (Objective-C++), compiled ONLY
when `RFTRACE_ENABLE_METAL=ON` AND `APPLE`, linking `-framework Metal -framework
Foundation`. When enabled, CMake defines `RFTRACE_HAVE_METAL=1`. The `.mm` exposes a
factory `std::unique_ptr<IBackend> makeMetalBackend()` declared in a small header
(`metal_backend.hpp`) guarded by `RFTRACE_HAVE_METAL`. `makeBackend(Backend::Metal, ...)`
calls `makeMetalBackend()` when compiled, else falls back to CPU (preserving existing
fallback behaviour when `allowFallback` is true, else throws). `backendAvailable(Metal)`
returns true only when `RFTRACE_HAVE_METAL` AND a Metal device actually exists at runtime
(`MTLCreateSystemDefaultDevice()` non-null and `supportsRaytracing`). The kernel source is
compiled at RUNTIME via `newLibraryWithSource:` (embedded as a string constant) — no
offline metallib.

### D4. Precision: float32 GPU, double C++, tolerance parity
Metal is float32; the CPU reference is double. The GPU acceleration structure and kernel
work in float. `Vec3`/`Hit` stay double on the C++ side; values convert to float only when
filling Metal buffers and convert back to double when reading hits out. Parity tests
compare hit/miss EXACTLY, require the SAME triangle index for well-separated geometry, and
compare `t` within tolerance (absolute ~1e-2 m and/or relative ~1e-4).

### D5. Buffer layout (host struct matches kernel struct)
Define matching structs on host and in the kernel, using `packed_float3` (or explicit
`float x,y,z`) to avoid `float3` 16-byte padding mismatches:

```
Ray  { packed_float3 origin; packed_float3 dir; float tmin; float tmax; }
Hit  { float t; uint prim; float u; float v; uint valid; }
```

Buffers use `MTLResourceStorageModeShared` (unified memory on Apple Silicon), so the host
writes rays and reads hits without explicit blits.

### D6. Tests: build only under Metal, skip at runtime without a GPU
The parity/behaviour test file builds and runs ONLY under `RFTRACE_ENABLE_METAL`; it is
guarded with `#if RFTRACE_HAVE_METAL`, and at runtime calls `GTEST_SKIP()` when
`backendAvailable(Backend::Metal)` is false (no GPU). The default CI (no Metal flag) is
unaffected: the existing 73 C++ tests + Python tests keep passing.

### D7. `just metal` recipe, not in default CI
Add a `just metal` recipe that configures with `-DRFTRACE_ENABLE_METAL=ON` in `build-metal`
and runs its ctest. Metal is NOT added to the default `ci` recipe.

## Risks / Trade-offs

- **float32 vs double parity** — GPU traversal in float can disagree with the double CPU
  reference near grazing angles / shared edges. Mitigation: parity asserts require
  well-separated geometry for exact triangle-index equality and use a tolerance on `t`;
  ambiguous edge-on cases are excluded from the strict comparison.
- **`packed_float3` padding mismatch** — a `float3` on host would be 16 bytes and desync
  from the kernel. Mitigation: use `packed_float3`/explicit scalars in the shared structs
  (D5) and static-assert sizes on the host.
- **Runtime shader compile failure** — `newLibraryWithSource:` can fail on an unexpected
  toolchain. Mitigation: check the returned `NSError`, surface a clear message, and treat
  the backend as unavailable rather than crashing.
- **No GPU / older Mac** — `backendAvailable(Metal)` gates on an actual device +
  `supportsRaytracing`; `makeBackend` falls back to CPU; tests `GTEST_SKIP()`.
- **Additive API surface** — adding virtuals to `IBackend` is ABI-affecting but the project
  builds from source and the methods are non-pure with defaults, so existing backends and
  callers need no changes.

## Migration Plan

Additive and backward compatible. Default builds ignore Metal entirely
(`RFTRACE_HAVE_METAL` undefined; the `.mm` is not compiled). Enabling
`-DRFTRACE_ENABLE_METAL=ON` compiles the backend and its tests. No data migration; no
changes to serialized result formats.

## Open Questions

None. All Phase 4 decisions (D1-D7) are resolved above.
