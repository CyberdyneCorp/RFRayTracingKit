## 1. Batched-query API (`ray-simulation`)

- [x] 1.1 Add two NON-pure virtual methods to `IBackend` in `include/rftrace/backend.hpp`:
  `std::vector<Hit> closestHitBatch(const std::vector<Ray>&) const` and
  `std::vector<char> occludedBatch(const std::vector<Ray>&) const` (D1)
- [x] 1.2 Provide default implementations that loop the single-ray `closestHit`/`occluded`
  (define in a `.cpp`, e.g. `src/backends/backend.cpp` or the CPU TU), so the CPU backend
  inherits them unchanged
- [x] 1.3 Add a unit test asserting the default batch methods return results identical to
  looping the single-ray methods on the CPU backend (regression for D1)

## 2. Build system (`metal-backend`)

- [x] 2.1 Implement `RFTRACE_ENABLE_METAL` in CMake: when `ON` AND `APPLE`, compile
  `src/backends/metal/metal_backend.mm`, define `RFTRACE_HAVE_METAL=1`, and link
  `-framework Metal -framework Foundation` (D3)
- [x] 2.2 Ensure the default build (no flag) does NOT compile the `.mm` and leaves
  `RFTRACE_HAVE_METAL` undefined; confirm the existing 73 C++ tests + Python tests pass
- [x] 2.3 Add a `just metal` recipe: configure `-DRFTRACE_ENABLE_METAL=ON` into `build-metal`
  and run its ctest; do NOT add Metal to the default `ci` recipe (D7)

## 3. Metal backend — acceleration structure (`metal-backend`)

- [x] 3.1 Add `metal_backend.hpp` declaring `std::unique_ptr<IBackend> makeMetalBackend()`,
  guarded by `RFTRACE_HAVE_METAL` (D3)
- [x] 3.2 In `metal_backend.mm`, acquire the device, build a `packed_float3` vertex buffer
  and `uint` index buffer from the scene `Triangle`s (float conversion), with primitive
  index == triangle index (D2, D4, D5)
- [x] 3.3 Build an `MTLAccelerationStructure` primitive AS via
  `MTLAccelerationStructureTriangleGeometryDescriptor` + command encoder (scratch buffer,
  optional compaction) in `build()` (D2)

## 4. Metal backend — kernels (`metal-backend`)

- [x] 4.1 Embed the `.metal` kernel source as a string constant and compile it at RUNTIME
  via `newLibraryWithSource:`, checking `NSError` and failing gracefully (D3)
- [x] 4.2 Define matching host/kernel `Ray`/`Hit` structs with `packed_float3` and
  static-assert host sizes; use `MTLResourceStorageModeShared` buffers (D5)
- [x] 4.3 Write the closest-hit kernel using `raytracing::intersector<triangle_data>` over
  `acceleration_structure<>`, writing `{t, prim, u, v, valid}` per ray (D2)
- [x] 4.4 Write the occlusion path using `accept_any_intersection(true)` (D2)
- [x] 4.5 Override `closestHitBatch`/`occludedBatch` with a single GPU dispatch (fill shared
  ray buffer, dispatch, read hits, convert float->double); implement single-ray
  `closestHit`/`occluded` via a one-element batch; set `kind()` to `Backend::Metal` (D1, D4)

## 5. Backend selection & availability (`metal-backend`)

- [x] 5.1 Add `backendAvailable(Backend::Metal)` following the Embree pattern: true only when
  `RFTRACE_HAVE_METAL` AND a Metal device exists at runtime with `supportsRaytracing` (D3)
- [x] 5.2 Wire `makeBackend(Backend::Metal, ...)` to call `makeMetalBackend()` when compiled,
  else fall back to CPU when `allowFallback`, else throw (preserve existing behaviour) (D3)

## 6. Parity & skip tests (`ray-simulation`, `metal-backend`)

- [x] 6.1 Add a test file guarded by `#if RFTRACE_HAVE_METAL` that `GTEST_SKIP()`s when
  `backendAvailable(Backend::Metal)` is false (D6)
- [x] 6.2 Build a scene of well-separated triangles, run identical ray batches on CPU and
  Metal, and assert hit/miss matches exactly, the SAME triangle index, and `t` within
  tolerance (abs ~1e-2 m / rel ~1e-4) (D4)
- [x] 6.3 Add an occlusion parity test (`occludedBatch` CPU vs Metal) over blocked and
  clear segments (D4)

## 7. Verify

- [x] 7.1 Default build + ctest: existing 73 C++ tests + Python tests pass, no Metal compiled
- [x] 7.2 Metal build (`just metal` / build-metal): configure, build, ctest green (parity
  tests pass on the M2 Max GPU; skip cleanly if no GPU)
- [x] 7.3 Update README/docs with the Metal build flag, `just metal` recipe, and the batched
  API note
