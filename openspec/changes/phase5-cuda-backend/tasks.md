## 1. Build system (`cuda-backend`)

- [x] 1.1 Implement `RFTRACE_ENABLE_CUDA` in CMake: when `ON`, `enable_language(CUDA)`,
  `find_package(CUDAToolkit REQUIRED)`, locate OptiX via `OptiX_INSTALL_DIR`; compile the device
  program (`src/backends/cuda/cuda_programs.cu`) to PTX and the host TU
  (`src/backends/cuda/cuda_backend.cpp`); define `RFTRACE_HAVE_CUDA=1` and link CUDA/OptiX (D2)
- [x] 1.2 Ensure the DEFAULT build (both `RFTRACE_ENABLE_CUDA` and `RFTRACE_ENABLE_OPENCL` OFF) does
  NOT compile any CUDA source and leaves `RFTRACE_HAVE_CUDA` undefined; confirm the existing 125 C++
  tests pass (primary regression guard) (D7)
- [x] 1.3 Add a `just cuda` recipe: configure `-DRFTRACE_ENABLE_CUDA=ON` (+ `OptiX_INSTALL_DIR`)
  into `build-cuda` and run its ctest; do NOT add CUDA to the default `ci` recipe. On non-NVIDIA
  hosts this recipe is expected to fail to configure (D8)

## 2. Factory header & backend selection (`cuda-backend`)

- [x] 2.1 Add `include/rftrace/backends/cuda_backend.hpp` declaring
  `std::unique_ptr<IBackend> makeCudaBackend()` and `bool cudaDeviceAvailable()`, guarded by
  `#if RFTRACE_HAVE_CUDA` (analogous to `metal_backend.hpp`) (D2)
- [x] 2.2 Add `backendAvailable(Backend::CUDA)` in `cpu_backend.cpp` following the Metal pattern:
  true only when `RFTRACE_HAVE_CUDA` AND `cudaDeviceAvailable()` (device present + OptiX init ok) (D3)
- [x] 2.3 Wire `makeBackend(Backend::CUDA, ...)` to call `makeCudaBackend()` when compiled and a
  device exists, inside try/catch: fall back to CPU when `allowFallback`, else rethrow; when not
  compiled, fall back to CPU (or throw when `!allowFallback`) — mirror the Metal branch (D3)

## 3. CUDA backend — acceleration structure (`cuda-backend`)

- [x] 3.1 In `cuda_backend.cpp`, initialize CUDA + the OptiX context; build float32 vertex and
  `unsigned int` index device buffers from the scene `Triangle`s (float conversion), primitive index
  == triangle index (D1, D4, D5)
- [x] 3.2 Build the OptiX GAS via `OptixBuildInput` (triangle array) + `optixAccelBuild` with
  compaction, storing the resulting `OptixTraversableHandle` in `build()` (D1)

## 4. CUDA backend — device programs & kernels (`cuda-backend`)

- [x] 4.1 Author the device program `src/backends/cuda/cuda_programs.cu` (raygen + closesthit + miss,
  plus anyhit/miss for occlusion) compiled to PTX and loaded via `optixModuleCreate`, failing
  gracefully on log/return errors (D1, D2)
- [x] 4.2 Define matching host/device POD `Ray`/`Hit` structs with explicit scalars (avoid `float3`
  alignment surprises) and static-assert host sizes (D5)
- [x] 4.3 Write the closest-hit path (`OPTIX_RAY_FLAG_NONE`, writing `{t, prim, u, v, valid}`) and
  the occlusion path (`OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT`) (D1)
- [x] 4.4 Override `closestHitBatch`/`occludedBatch` with a single `optixLaunch` (upload rays, launch,
  read hits, convert float->double); implement single-ray `closestHit`/`occluded` via a one-element
  batch; set `kind()` to `Backend::CUDA` (D6)

## 5. Parity & skip tests (`ray-simulation`, `cuda-backend`)

- [x] 5.1 Add `tests/test_cuda_parity.cpp` guarded by `#if RFTRACE_HAVE_CUDA` that `GTEST_SKIP()`s
  when `backendAvailable(Backend::CUDA)` is false; wire it into `tests/CMakeLists.txt` with
  `gtest_discover_tests(... PRE_TEST)` (D7)
- [x] 5.2 Build a scene of well-separated triangles, run identical ray batches on CPU and CUDA, and
  assert hit/miss matches exactly, the SAME triangle index, and `t` within tolerance
  `|dt| <= max(1e-2 m, 1e-4*|t|)` (D4)
- [x] 5.3 Add an occlusion parity test (`occludedBatch` CPU vs CUDA) over blocked and clear
  segments, and an empty-batch test (D4, D6)

## 6. Verify

- [x] 6.1 Default build + ctest: existing 125 C++ tests pass, NO CUDA compiled, `RFTRACE_HAVE_CUDA`
  undefined (the non-negotiable regression guard) — verified on this host
- [ ] 6.2 CUDA build (`just cuda` / build-cuda) on NVIDIA hardware: configure, build, ctest green
  (parity tests pass on the GPU; skip cleanly if no GPU). **UNVERIFIED on this non-NVIDIA host — to
  be validated on NVIDIA + OptiX hardware. NOT compiled or run here.** (On this host,
  `-DRFTRACE_ENABLE_CUDA=ON` correctly fails to configure: `find_package(CUDAToolkit REQUIRED)`
  errors with "Could not find nvcc", as expected.)
- [x] 6.3 Update README/docs with the CUDA build flags (`RFTRACE_ENABLE_CUDA`, `OptiX_INSTALL_DIR`),
  the `just cuda` recipe, and the UNVERIFIED-on-non-NVIDIA note
