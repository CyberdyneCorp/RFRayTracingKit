# opencl-backend Specification

## Purpose
TBD - created by archiving change phase6-opencl-backend. Update Purpose after archive.
## Requirements
### Requirement: OpenCL GPU ray-traversal backend
The library SHALL provide an OpenCL GPU backend that implements the `IBackend` interface using a
software BVH traversal kernel (there is NO hardware ray tracing in OpenCL 1.2), compiled only when
`RFTRACE_ENABLE_OPENCL=ON` AND `find_package(OpenCL)` succeeds (defining `RFTRACE_HAVE_OPENCL`). The
backend SHALL accelerate ray traversal only and SHALL NOT change RF physics, the scene model, the
simulator, or result formats. This backend is VERIFIED on Apple OpenCL 1.2 (M2 Max) and portable to
other OpenCL 1.2+ GPUs.

#### Scenario: OpenCL backend implements the backend contract
- **WHEN** the project is built with `RFTRACE_ENABLE_OPENCL=ON` on a host with an OpenCL device and
  an OpenCL backend is created
- **THEN** it SHALL implement `build`, `closestHit`, `occluded`, the batched query methods, and
  report `kind()` as `Backend::OpenCL`

#### Scenario: RF core is unchanged by the OpenCL build
- **WHEN** the project is built with `RFTRACE_ENABLE_OPENCL=OFF` (default)
- **THEN** the OpenCL implementation SHALL NOT be compiled, `RFTRACE_HAVE_OPENCL` SHALL be undefined,
  and the library and its existing C++ test suite (125 tests) SHALL build and pass unchanged

### Requirement: Custom flat BVH device buffers from scene triangles
The OpenCL backend SHALL build a custom flat BVH from the scene triangles and upload it to device
memory as float32 buffers (a node buffer with per-node AABB min/max, left-child index, and leaf
start/count; a triangle-vertex buffer; and a triangle-index permutation buffer), such that the
traversal's primitive index equals the corresponding `Triangle` index in the built scene. Exposing
the flat BVH from the CPU `BVH` SHALL be additive and SHALL NOT change existing CPU BVH behaviour.

#### Scenario: Build maps primitive index to triangle index
- **WHEN** `build(triangles)` is called on the OpenCL backend
- **THEN** it SHALL construct flat device buffers whose primitive index N corresponds to
  `triangles[N]`, so a GPU hit resolves directly to that triangle

#### Scenario: Hit reports the source triangle index
- **WHEN** a ray intersects triangle N via the OpenCL traversal kernel
- **THEN** the returned `Hit` SHALL reference triangle index N (the same index the CPU backend
  reports for well-separated geometry)

### Requirement: Iterative OpenCL C traversal kernel and batched dispatch
The OpenCL backend SHALL traverse its flat BVH with an iterative OpenCL C kernel (C99, OpenCL 1.2)
that uses an explicit fixed-size stack and NO recursion, running a triangle-intersection test over
leaf primitives. A closest-hit kernel SHALL keep the nearest hit; an any-hit occlusion kernel SHALL
early-exit at the first triangle intersected within `[tMin, tMax]`. The kernel source SHALL be
compiled at RUNTIME via `clCreateProgramWithSource` + `clBuildProgram`. The backend SHALL override
`closestHitBatch` and `occludedBatch` to service a whole batch in a single device dispatch
(`clEnqueueNDRangeKernel`).

#### Scenario: Kernel compiles at runtime
- **WHEN** the OpenCL backend initializes on a host with an OpenCL device
- **THEN** it SHALL compile its embedded kernel source at runtime via `clCreateProgramWithSource`
  and SHALL capture the build log and surface a clear error (treating the backend as unavailable)
  rather than crashing if `clBuildProgram` fails

#### Scenario: Batch serviced in a single dispatch
- **WHEN** a batch of rays is submitted to the OpenCL backend's batched methods
- **THEN** the backend SHALL upload the rays once, service the whole batch in a single
  `clEnqueueNDRangeKernel` (not per-ray host<->device round-trips), and return one result per input
  ray, in input order

#### Scenario: Occlusion stops at first hit
- **WHEN** an occlusion query runs over a ray whose segment is blocked
- **THEN** the any-hit kernel SHALL report the ray as occluded, early-exiting at the first triangle
  hit within `[tMin, tMax]` without computing the closest hit

### Requirement: Host and device buffer layout match
The OpenCL backend SHALL define matching node, ray, and hit POD structs on the host and in the kernel
using explicit float scalars (avoiding `float3`/`float4` alignment mismatches), and SHALL
static-assert the host struct sizes. The C++ `Vec3`/`Hit` types SHALL remain double precision; values
SHALL convert to float only inside the device buffers.

#### Scenario: Layout avoids alignment desync
- **WHEN** rays are uploaded to the device ray buffer and hits are read back
- **THEN** the host struct layout SHALL match the kernel struct, so ray fields and hit fields are
  interpreted correctly on both sides

#### Scenario: Precision is converted at the buffer boundary
- **WHEN** double-precision rays are dispatched and float hits are read back
- **THEN** the backend SHALL convert double to float when filling buffers and float to double when
  producing `Hit`, leaving the public C++ types double

### Requirement: OpenCL backend selection and runtime availability
`backendAvailable(Backend::OpenCL)` SHALL return true only when `RFTRACE_HAVE_OPENCL` is defined AND
an OpenCL platform with a usable device exists at runtime. `makeBackend(Backend::OpenCL,
allowFallback)` SHALL return the OpenCL backend when compiled and available, and SHALL otherwise fall
back to the CPU backend when `allowFallback` is true or throw a clear error when it is false,
preserving the existing fallback behaviour (mirroring the Metal backend).

#### Scenario: Availability reflects a real device
- **WHEN** the build defines `RFTRACE_HAVE_OPENCL` but no OpenCL device is present at runtime
- **THEN** `backendAvailable(Backend::OpenCL)` SHALL return false

#### Scenario: Builds and selects when OpenCL is present
- **WHEN** the project is built with `RFTRACE_ENABLE_OPENCL=ON` on a host with an OpenCL device and
  `makeBackend(Backend::OpenCL, ...)` is called
- **THEN** it SHALL return the OpenCL backend reporting `kind()` as `Backend::OpenCL`

#### Scenario: Falls back to CPU when OpenCL is absent
- **WHEN** `makeBackend(Backend::OpenCL, true)` is called in a build without OpenCL or without a device
- **THEN** it SHALL return the CPU backend rather than failing

#### Scenario: Explicit no-fallback errors clearly
- **WHEN** `makeBackend(Backend::OpenCL, false)` is called and OpenCL is unavailable
- **THEN** it SHALL throw a clear error and SHALL NOT crash

