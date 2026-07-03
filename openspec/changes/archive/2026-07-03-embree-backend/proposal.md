## Why

The reference CPU backend is a scalar nanort-style BVH — the benchmark measured it at ~0.1 Mray/s
single-thread, the slow part of a CPU full run. Intel **Embree** is a highly-optimized CPU
ray-tracing kernel library (SIMD-accelerated BVH traversal) typically 1–2 orders of magnitude faster
on the same CPU, and — unlike the GPU backends — its committed scenes are **thread-safe for concurrent
queries**, so it composes with the existing `threadCount` parallelism. The project already declares
`Backend::Embree`, the `RFTRACE_ENABLE_EMBREE` flag, `find_package(embree 4)`, and the link, but
`makeBackend(Backend::Embree)` today just maps to the reference CPU BVH — a placeholder. This change
replaces the placeholder with a real Embree traversal backend.

## What Changes

- Implement a real **Embree backend** (`IBackend`): `build()` creates an `RTCDevice`/`RTCScene` with a
  triangle geometry (float32 vertex + `unsigned` index buffers, primitive index == `Triangle` index)
  and commits it; `closestHit()` uses `rtcIntersect1`, `occluded()` uses `rtcOccluded1`; the batched
  methods service a whole batch (loop, or SIMD packet/stream variants). Compiled only when
  `RFTRACE_ENABLE_EMBREE=ON` and Embree 4 is found (`RFTRACE_HAVE_EMBREE`).
- Route `makeBackend(Backend::Embree)` to the real backend when compiled + available (device creation
  succeeds), preserving the CPU fallback (mirroring the CUDA/Metal/OpenCL factories). `kind()` returns
  `Backend::Embree`; `backendAvailable(Backend::Embree)` reflects a real device.
- Precision boundary matches the other float backends: `Vec3`/`Hit` stay double; values convert to
  float only in the Embree buffers. RF physics, the scene model, the simulator, and result formats
  are unchanged.
- **The pure-C++20 CPU BVH is untouched and remains the default.** `Backend::CPU` stays the
  always-compiled, zero-dependency backend; it is still the reference oracle every other backend
  (Embree included) is validated against, and still the fallback whenever an optional backend is
  unavailable. Embree is strictly additive and opt-in (`RFTRACE_ENABLE_EMBREE`, default OFF): with the
  flag off, nothing changes; with it on but no Embree device, `makeBackend(Embree)` falls back to the
  CPU backend. So a build with no Embree, no CUDA/Metal/OpenCL, and no GPU still runs entirely on the
  portable C++ CPU path.
- Add a **CPU-vs-Embree parity** suite mirroring the CUDA/Metal/OpenCL parity tests (float-vs-double
  tolerance, matching triangle indices for well-separated geometry, occlusion parity, determinism),
  and wire the default build so it stays unchanged when the flag is off. Fully buildable and testable
  on this host (Embree 4.4.0 via vcpkg; x86 CPU).

## Capabilities

### New Capabilities
- `embree-backend`: an Intel-Embree CPU traversal backend implementing `IBackend`, selected via
  `Backend::Embree`, validated against the reference CPU BVH by a parity suite, and compiled behind
  `RFTRACE_ENABLE_EMBREE` — a fast, portable CPU backend that is thread-safe for concurrent queries.

### Modified Capabilities
<!-- None. RF physics, scene, simulator, and result requirements are unchanged. The batched-query
     and backend-selection contracts in ray-simulation already accommodate a new backend; this adds
     a concrete backend behind the existing enum/flag without changing those requirements. -->

## Impact

- **Code**: new `src/backends/embree/embree_backend.cpp` + `include/rftrace/backends/embree_backend.hpp`
  (guarded factory, mirroring `cuda_backend.hpp`); wire `makeBackend`/`backendAvailable` for
  `Backend::Embree` in `cpu_backend.cpp`; the existing CMake `RFTRACE_ENABLE_EMBREE` block builds the
  new TU; a new `tests/test_embree_parity.cpp`; a `just embree` recipe.
- **Public API**: none changed — `Backend::Embree` already exists; behaviour changes from
  "maps to CPU" to "real Embree backend when enabled".
- **Deps**: Embree 4 (vcpkg `embree` 4.4.0), only when `RFTRACE_ENABLE_EMBREE=ON`; default build
  unaffected. Not part of the default `ci` recipe initially (can be added later — it is CPU-only and
  CI-capable).
- **Risk**: float-vs-double parity classification (contained by the established parity-suite pattern);
  Embree API/version specifics (Embree 4 device/scene/geometry lifecycle) — validated on real
  hardware here.
