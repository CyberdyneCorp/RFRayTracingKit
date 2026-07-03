## 1. Backend implementation

- [x] 1.1 Add `include/rftrace/backends/embree_backend.hpp` — guarded factory (`makeEmbreeBackend()`, `embreeDeviceAvailable()`) under `#if RFTRACE_HAVE_EMBREE`, mirroring `cuda_backend.hpp`.
- [x] 1.2 Add `src/backends/embree/embree_backend.cpp` — `EmbreeBackend : IBackend`: create one `RTCDevice` (error callback), `build()` → `RTCScene` with a triangle geometry (float3 vertex + `unsigned[3]` index, primID == triangle index) committed.
- [x] 1.3 Implement `closestHit` (`rtcIntersect1` → `Hit`) and `occluded` (`rtcOccluded1`), honouring `[tMin,tMax]`; convert double↔float only at the Embree buffers. Default the batched methods (loop) or add `rtcIntersect4/8` packets.
- [x] 1.4 Use robust scene/geometry flags (`RTC_SCENE_FLAG_ROBUST`) to minimise float-precision borderline cases; release scene/device in the destructor.

## 2. Factory wiring & build

- [x] 2.1 Wire `makeBackend(Backend::Embree)` and `backendAvailable(Backend::Embree)` in `cpu_backend.cpp` to the real backend when `RFTRACE_HAVE_EMBREE` + available, with the same try/catch CPU-fallback as the CUDA/Metal/OpenCL paths (replacing the current placeholder that maps Embree to CPU).
- [x] 2.2 Extend the existing `RFTRACE_ENABLE_EMBREE` CMake block to compile `src/backends/embree/embree_backend.cpp` into the library.
- [x] 2.3 Confirm the default build (flag OFF) is byte-for-byte unchanged: `RFTRACE_HAVE_EMBREE` undefined, `Backend::Embree` still resolves to the CPU reference via fallback, existing suite passes.

## 3. Parity tests

- [x] 3.1 Add `tests/test_embree_parity.cpp` mirroring `test_cuda_parity.cpp`/`test_opencl_parity.cpp`: same scene/ray builders, tolerances, and hard-vs-borderline classification; skips at runtime when no Embree device, no-op when the flag is off.
- [x] 3.2 Assert closest-hit + occlusion parity vs the reference CPU BVH (only non-borderline disagreements must be zero) and determinism across repeated runs.
- [x] 3.3 Register the test; build with `-DRFTRACE_ENABLE_EMBREE=ON` (vcpkg Embree 4.4.0) and run the parity suite on this host.

## 4. Docs, recipe & archive

- [x] 4.1 Add a `just embree` recipe (configure with the flag + vcpkg, build, run the parity suite).
- [x] 4.2 Record a quick CPU-vs-Embree traversal speedup number (e.g. via the low-level or sim benchmark) for the roadmap note.
- [x] 4.3 Update README + `openspec/project.md`: Embree moves from "flag maps to CPU" to a real backend; **explicitly reaffirm the pure-C++20 CPU BVH remains the always-available, zero-dependency default, reference oracle, and fallback**. Run `openspec validate --strict` and archive the change.
