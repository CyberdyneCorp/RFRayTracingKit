## Context

`Backend::Embree`, `RFTRACE_ENABLE_EMBREE`, `find_package(embree 4 CONFIG)`, the link, and
`RFTRACE_HAVE_EMBREE` are already wired, but `makeBackend(Backend::Embree)` returns the reference CPU
`CpuBackend` (a placeholder). The project already has three float-precision traversal backends
(CUDA/Metal/OpenCL) each following the same shape: an `IBackend` implementation behind a compile flag,
a guarded factory header, a `makeBackend`/`backendAvailable` wiring in `cpu_backend.cpp`, and a
parity suite vs the double reference. Embree is the CPU member of that family — and, being CPU and
thread-safe, is also the one that can compose with the `threadCount` parallelism. Embree 4.4.0 is in
vcpkg and runs on this x86 host, so this backend is fully buildable and testable here.

## Goals / Non-Goals

**Goals:**
- A real `EmbreeBackend : IBackend` (build/closestHit/occluded + batched) over Embree 4.
- `makeBackend`/`backendAvailable` route `Backend::Embree` to it, with CPU fallback (mirror the
  CUDA/Metal/OpenCL factories exactly).
- A CPU-vs-Embree parity suite using the established borderline-classification rule.
- Keep the default build (flag off) byte-for-byte unchanged; RF physics/results untouched.

**Non-Goals:**
- No change to RF physics, scene, simulator, results, or the `IBackend` contract.
- Not (in this change) adding Embree to the Phase-2 threading gate — noted as a follow-up (Embree is
  thread-safe, so it is a candidate, but the gating decision is separate).
- Not exhaustively using Embree's packet/stream/curve/instancing features; triangles + single-ray
  (or a simple packet) is enough for parity and speed in v1.
- Not adding Embree to the default `ci` recipe in this change (can follow once green).

## Decisions

### D1 — Mirror the existing float-backend structure exactly
New `include/rftrace/backends/embree_backend.hpp` (guarded factory `makeEmbreeBackend()` +
`embreeDeviceAvailable()`, `#if RFTRACE_HAVE_EMBREE`) and `src/backends/embree/embree_backend.cpp`
(the `EmbreeBackend` class). Wire `makeBackend(Backend::Embree)` and `backendAvailable` in
`cpu_backend.cpp` with the same try/catch-fallback used for CUDA/Metal/OpenCL. **Why:** consistency —
a reviewer and future maintainer see one pattern across all optional backends.

### D2 — Embree object lifecycle: device once, scene per build
Create one `RTCDevice` in the constructor (fail → treat as unavailable). `build(triangles)` releases
any prior scene, creates an `RTCScene`, attaches one `RTC_GEOMETRY_TYPE_TRIANGLE` geometry with a
`RTC_BUFFER_TYPE_VERTEX` float3 buffer (padded per Embree's 16-byte vertex requirement — use
`rtcSetNewGeometryBuffer` with the documented stride) and an `RTC_BUFFER_TYPE_INDEX` `unsigned[3]`
buffer where triplet N = (3N, 3N+1, 3N+2) so `primID == N`, then `rtcCommitGeometry` +
`rtcAttachGeometry` + `rtcCommitScene`. Set an error callback that surfaces failures. **Why:** the
primID→Triangle mapping is the contract; committing per build matches the other backends' rebuild
semantics (and the new Simulator backend cache reuses the built backend across runs).

### D3 — Query mapping and precision
`closestHit`: fill an `RTCRayHit` from the double ray (origin/dir → float, `tnear=tMin`,
`tfar=tMax`), `rtcIntersect1`; on hit (`geomID != RTC_INVALID_GEOMETRY_ID`) produce a double `Hit`
with `t=ray.tfar`, `u/v` from `hit.u/v`, `triangle=primID`. `occluded`: fill an `RTCRay`,
`rtcOccluded1`, occluded iff `tfar` was set to `-inf`. Batched methods loop these (v1) or use
`rtcIntersect4/8` packets (optional optimization); either way results are index-aligned with input.
`Vec3`/`Hit` stay double. **Why:** same float boundary as the other backends → same parity rule.

### D4 — Availability + fallback
`embreeDeviceAvailable()` = can create (and immediately release) an `RTCDevice`.
`backendAvailable(Backend::Embree)` returns that when `RFTRACE_HAVE_EMBREE`, else false.
`makeBackend(Backend::Embree, allowFallback)` returns the Embree backend when available; on
construction failure, falls back to CPU when `allowFallback`, else rethrows — identical to the
CUDA/Metal/OpenCL paths. **Why:** preserve the established selection/fallback contract.

### D5 — Parity suite reuses the borderline rule
`tests/test_embree_parity.cpp` mirrors `test_cuda_parity.cpp` / `test_opencl_parity.cpp`: the same
scene/ray builders, the same tolerances (`kAbsT`, `kEdgeEps`, `kBoundaryEps`, `kTieTol`), the same
"hard vs borderline" classification, plus determinism. Tests skip at runtime when no Embree device is
available and compile to a no-op when the flag is off. **Why:** Embree is float32 like the GPU
backends, so exact bit-equality with the double BVH is not expected; the proven parity rule applies.

## Risks / Trade-offs

- **[float-vs-double disagreements read as failures]** → reuse the borderline-classification parity
  rule (D5); assert only non-borderline (hard) disagreements are zero.
- **[Embree vertex/stride/geometry-lifecycle mistakes]** → follow the Embree 4 buffer/commit protocol
  (padded vertex stride, commit geometry+scene); validate on real hardware here; an error callback
  surfaces device/commit errors as backend-unavailable rather than crashes.
- **[Scene-rebuild cost with the Simulator backend cache]** → the cache reuses the built backend
  across runs on an unchanged scene, so the commit cost is paid once (no new concern).
- **[Thread-safety expectations]** → Embree queries on a committed scene are concurrency-safe, but
  this change does NOT itself enable Embree in the threading gate; that is a separate, additive step.

## Migration Plan

- PR: implement the backend + factory wiring + parity suite + `just embree`, behind the existing
  `RFTRACE_ENABLE_EMBREE` flag (default OFF → default build unchanged). Verify: build with the flag on
  (vcpkg Embree 4.4.0), run the parity suite here, and confirm the default suite is unaffected.
- Follow-ups (separate): add Embree to the CI matrix; consider including `Backend::Embree` in the
  Phase-2 threading gate (it is thread-safe) for a fast, portable, multi-threaded CPU path.

## Open Questions

- Single-ray (`rtcIntersect1`) vs packet (`rtcIntersect4/8/16`) for the batched methods in v1 — start
  single-ray for simplicity/correctness; add packets as an optimization once parity is green.
- Whether to expose an Embree build-quality / robustness flag (e.g. `RTC_BUILD_QUALITY_HIGH`,
  `RTC_SCENE_FLAG_ROBUST`) — default to robust to minimize float-precision borderline cases.
