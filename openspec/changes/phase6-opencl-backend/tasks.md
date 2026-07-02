## 1. Build system (`opencl-backend`)

- [x] 1.1 Implement `RFTRACE_ENABLE_OPENCL` in CMake: when `ON`, `find_package(OpenCL REQUIRED)`,
  compile the host TU (`src/backends/opencl/opencl_backend.cpp`), link `OpenCL::OpenCL`, define
  `RFTRACE_HAVE_OPENCL=1` plus `CL_SILENCE_DEPRECATION` and `CL_TARGET_OPENCL_VERSION=120` to
  pin/quiet the OpenCL 1.2 API on macOS (D2)
- [x] 1.2 Ensure the DEFAULT build (both `RFTRACE_ENABLE_OPENCL` and `RFTRACE_ENABLE_CUDA` OFF) does
  NOT compile any OpenCL source and leaves `RFTRACE_HAVE_OPENCL` undefined; confirm the existing 125
  C++ tests pass (primary regression guard) (D7)
- [x] 1.3 Add a `just opencl` recipe: configure `-DRFTRACE_ENABLE_OPENCL=ON` into `build-opencl` and
  run its ctest; do NOT add OpenCL to the default `ci` recipe (D8)

## 2. Factory header & backend selection (`opencl-backend`)

- [x] 2.1 Add `include/rftrace/backends/opencl_backend.hpp` declaring
  `std::unique_ptr<IBackend> makeOpenclBackend()` and `bool openclDeviceAvailable()`, guarded by
  `#if RFTRACE_HAVE_OPENCL` (analogous to `metal_backend.hpp`) (D2)
- [x] 2.2 Add `backendAvailable(Backend::OpenCL)` in `cpu_backend.cpp` following the Metal pattern:
  true only when `RFTRACE_HAVE_OPENCL` AND `openclDeviceAvailable()` (a platform + GPU device exists) (D3)
- [x] 2.3 Wire `makeBackend(Backend::OpenCL, ...)` to call `makeOpenclBackend()` when compiled and a
  device exists, inside try/catch: fall back to CPU when `allowFallback`, else rethrow; when not
  compiled, fall back to CPU (or throw when `!allowFallback`) — mirror the Metal branch (D3)

## 3. Flat BVH + device buffers (`opencl-backend`)

- [x] 3.1 Add an ADDITIVE public flat-BVH accessor to `BVH` (or build an equivalent flat BVH inside
  the OpenCL backend) exposing flat nodes (AABB min/max + left child + start/count), triangle
  vertices, and the triangle-index permutation, WITHOUT changing existing CPU BVH behaviour (D5)
- [x] 3.2 In `opencl_backend.cpp` `build()`, convert the flat BVH to float32 device buffers (nodes,
  triangle vertices, index permutation) and upload them via `clCreateBuffer`/`clEnqueueWriteBuffer`;
  keep the primitive/triangle index correspondence so a hit resolves to the source triangle (D1, D5)

## 4. OpenCL kernel & batched dispatch (`opencl-backend`)

- [x] 4.1 Author the embedded OpenCL C kernel string (C99, OpenCL 1.2, NO recursion): an iterative
  BVH traversal using an explicit fixed-size stack, a Moller-Trumbore triangle test, a closest-hit
  kernel and an any-hit occlusion kernel that early-exits at the first hit in `[tmin, tmax]` (D1)
- [x] 4.2 Compile the kernel at RUNTIME via `clCreateProgramWithSource` + `clBuildProgram`, capturing
  the build log and treating any build error as backend-unavailable (graceful failure, not a crash) (D1, D2)
- [x] 4.3 Define matching host/device POD `Ray`/`Hit`/`Node` structs with explicit float scalars
  (avoid `float3`/`float4` alignment surprises) and static-assert host sizes (D5)
- [x] 4.4 Override `closestHitBatch`/`occludedBatch` with a single `clEnqueueNDRangeKernel` (upload
  rays, one NDRange over `rays.size()` work-items, read hits back, convert float->double); implement
  single-ray `closestHit`/`occluded` via a one-element batch; set `kind()` to `Backend::OpenCL` (D6)

## 5. Parity & skip tests (`ray-simulation`, `opencl-backend`)

- [x] 5.1 Add `tests/test_opencl_parity.cpp` guarded by `#if RFTRACE_HAVE_OPENCL` that `GTEST_SKIP()`s
  when `backendAvailable(Backend::OpenCL)` is false; wire it into `tests/CMakeLists.txt` with
  `gtest_discover_tests(... PRE_TEST)` (D7)
- [x] 5.2 Build a scene of well-separated triangles, run identical ray batches on CPU and OpenCL, and
  assert hit/miss matches exactly, the SAME triangle index, and `t` within tolerance
  `|dt| <= max(1e-2 m, 1e-4*|t|)` (D4)
- [x] 5.3 Add an occlusion parity test (`occludedBatch` CPU vs OpenCL) over blocked and clear
  segments, and an empty-batch test (D4, D6)

## 6. Verify

- [x] 6.1 Default build + ctest: existing 125 C++ tests pass, NO OpenCL compiled, `RFTRACE_HAVE_OPENCL`
  undefined (the non-negotiable regression guard) — verified on this host
- [x] 6.2 OpenCL build (`just opencl` / build-opencl) on this host: configure with
  `-DRFTRACE_ENABLE_OPENCL=ON`, build, ctest green — the parity tests run on the Apple M2 Max GPU
  (OpenCL 1.2) and pass (skip cleanly only if no device) — VERIFIED here
- [x] 6.3 Update README/docs with the OpenCL build flags (`RFTRACE_ENABLE_OPENCL`), the `just opencl`
  recipe, and the VERIFIED-on-Apple-OpenCL-1.2 / portable-to-OpenCL-1.2+ note
