# cuda-backend Specification

## Purpose
Provide an optional NVIDIA CUDA/OptiX GPU backend that accelerates ray traversal behind the shared
`IBackend` contract, leaving RF physics, the scene model, the simulator, and result formats
unchanged. It is an opt-in build (`RFTRACE_ENABLE_CUDA=ON`) validated against the CPU reference via a
parity suite.
## Requirements
### Requirement: CUDA/OptiX GPU ray-traversal backend
The library SHALL provide a CUDA/OptiX GPU backend that implements the `IBackend` interface using
OptiX hardware ray tracing driven by CUDA kernels, compiled only when `RFTRACE_ENABLE_CUDA=ON` AND
the CUDA Toolkit and OptiX SDK are found (defining `RFTRACE_HAVE_CUDA`). The backend SHALL accelerate
ray traversal only and SHALL NOT change RF physics, the scene model, the simulator, or result
formats. The backend has been validated on NVIDIA hardware (GeForce RTX 5060, CUDA 12.0, driver
580.95.05, OptiX SDK 9.0.0) where the parity suite passes. The chosen OptiX SDK's `OPTIX_ABI_VERSION`
MUST be one the installed driver's OptiX runtime implements; otherwise `optixInit()` fails with
`OPTIX_ERROR_UNSUPPORTED_ABI_VERSION` and the backend reports itself unavailable (falling back to
CPU). Because the backend uses only the OptiX ≥ 7.7 host API, any SDK whose ABI the driver supports
is acceptable.

#### Scenario: CUDA backend implements the backend contract
- **WHEN** the project is built with `RFTRACE_ENABLE_CUDA=ON` on a host with CUDA + OptiX and a CUDA
  backend is created
- **THEN** it SHALL implement `build`, `closestHit`, `occluded`, the batched query methods, and
  report `kind()` as `Backend::CUDA`

#### Scenario: RF core is unchanged by the CUDA build
- **WHEN** the project is built with `RFTRACE_ENABLE_CUDA=OFF` (default)
- **THEN** the CUDA implementation SHALL NOT be compiled, `RFTRACE_HAVE_CUDA` SHALL be undefined, and
  the library and its existing C++ test suite (125 tests) SHALL build and pass unchanged

### Requirement: OptiX acceleration structure from scene triangles
The CUDA backend SHALL build an OptiX geometry acceleration structure (GAS, an
`OptixTraversableHandle`) from the scene triangles, using a float32 vertex buffer and an
`unsigned int` index buffer such that the OptiX primitive index equals the corresponding `Triangle`
index in the built scene.

#### Scenario: Build maps primitive index to triangle index
- **WHEN** `build(triangles)` is called on the CUDA backend
- **THEN** it SHALL construct a GAS whose primitive index N corresponds to `triangles[N]`, so a GPU
  hit (`optixGetPrimitiveIndex()`) resolves directly to that triangle

#### Scenario: Hit reports the source triangle index
- **WHEN** a ray intersects triangle N via the OptiX intersector
- **THEN** the returned `Hit` SHALL reference triangle index N (the same index the CPU backend
  reports for well-separated geometry)

### Requirement: CUDA/OptiX device programs and batched dispatch
The CUDA backend SHALL use CUDA/OptiX device programs (raygen, closest-hit, miss, and an anyhit/miss
pair for occlusion) launched via `optixLaunch` over a device buffer of rays to write hits. Closest-hit
SHALL use `OPTIX_RAY_FLAG_NONE`; occlusion SHALL use `OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT` so
traversal stops at the first hit within `[tMin, tMax]`. The device program SHALL be compiled to PTX
and loaded at runtime via `optixModuleCreate`. The backend SHALL override `closestHitBatch` and
`occludedBatch` to service a whole batch in a single device launch.

#### Scenario: Batch serviced in a single launch
- **WHEN** a batch of rays is submitted to the CUDA backend's batched methods
- **THEN** the backend SHALL upload the rays once, service the whole batch in a single `optixLaunch`
  (not per-ray host<->device round-trips), and return one result per input ray, in input order

#### Scenario: Occlusion stops at first hit
- **WHEN** an occlusion query runs over a ray whose segment is blocked
- **THEN** the traversal SHALL report the ray as occluded using
  `OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT` without requiring the closest hit

#### Scenario: Device program load failure is handled gracefully
- **WHEN** OptiX context creation, PTX/module load, or acceleration-structure build fails at runtime
- **THEN** the backend SHALL surface a clear error (treating the backend as unavailable) rather than
  crashing

### Requirement: Host and device buffer layout match
The CUDA backend SHALL define matching ray and hit POD structs on the host and device using explicit
float scalars (or a packed layout) to avoid CUDA `float3` alignment mismatches, and SHALL
static-assert the host struct sizes. The C++ `Vec3`/`Hit` types SHALL remain double precision; values
SHALL convert to float only inside the device buffers.

#### Scenario: Layout avoids alignment desync
- **WHEN** rays are uploaded to the device ray buffer and hits are read back
- **THEN** the host struct layout SHALL match the device struct, so ray fields and hit fields are
  interpreted correctly on both sides

#### Scenario: Precision is converted at the buffer boundary
- **WHEN** double-precision rays are dispatched and float hits are read back
- **THEN** the backend SHALL convert double to float when filling buffers and float to double when
  producing `Hit`, leaving the public C++ types double

### Requirement: CUDA backend selection and runtime availability
`backendAvailable(Backend::CUDA)` SHALL return true only when `RFTRACE_HAVE_CUDA` is defined AND a
CUDA device exists at runtime (device count > 0 and OptiX initialization succeeds).
`makeBackend(Backend::CUDA, allowFallback)` SHALL return the CUDA backend when compiled and available,
and SHALL otherwise fall back to the CPU backend when `allowFallback` is true or throw a clear error
when it is false, preserving the existing fallback behaviour (mirroring the Metal backend).

#### Scenario: Availability reflects a real device
- **WHEN** the build defines `RFTRACE_HAVE_CUDA` but no CUDA device is present at runtime
- **THEN** `backendAvailable(Backend::CUDA)` SHALL return false

#### Scenario: Builds and selects when CUDA is present
- **WHEN** the project is built with `RFTRACE_ENABLE_CUDA=ON` on a host with a CUDA device and OptiX,
  and `makeBackend(Backend::CUDA, ...)` is called
- **THEN** it SHALL return the CUDA/OptiX backend reporting `kind()` as `Backend::CUDA`

#### Scenario: Falls back to CPU when CUDA is absent
- **WHEN** `makeBackend(Backend::CUDA, true)` is called in a build without CUDA or without a GPU
- **THEN** it SHALL return the CPU backend rather than failing

#### Scenario: Explicit no-fallback errors clearly
- **WHEN** `makeBackend(Backend::CUDA, false)` is called and CUDA is unavailable
- **THEN** it SHALL throw a clear error and SHALL NOT crash

