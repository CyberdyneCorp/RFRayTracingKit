# metal-backend Specification

## Purpose
TBD - created by archiving change phase4-metal-backend. Update Purpose after archive.
## Requirements
### Requirement: Metal GPU ray-traversal backend
The library SHALL provide a Metal GPU backend that implements the `IBackend` interface using
hardware ray tracing, compiled only when `RFTRACE_ENABLE_METAL=ON` on an Apple platform
(defining `RFTRACE_HAVE_METAL`). The backend SHALL accelerate ray traversal only and SHALL
NOT change RF physics, the scene model, the simulator, or result formats.

#### Scenario: Metal backend implements the backend contract
- **WHEN** the project is built with `RFTRACE_ENABLE_METAL=ON` on Apple hardware and a Metal
  backend is created
- **THEN** it SHALL implement `build`, `closestHit`, `occluded`, the batched query methods,
  and report `kind()` as `Backend::Metal`

#### Scenario: RF core is unchanged by the Metal build
- **WHEN** the project is built with `RFTRACE_ENABLE_METAL=OFF` (default)
- **THEN** the Metal implementation SHALL NOT be compiled, `RFTRACE_HAVE_METAL` SHALL be
  undefined, and the library and its existing C++ test suite SHALL build and pass unchanged

### Requirement: Acceleration structure from scene triangles
The Metal backend SHALL build an `MTLAccelerationStructure` (primitive acceleration
structure) from the scene triangles, using a float32 vertex buffer and an index buffer such
that the primitive index equals the corresponding `Triangle` index in the built scene.

#### Scenario: Build maps primitive index to triangle index
- **WHEN** `build(triangles)` is called on the Metal backend
- **THEN** it SHALL construct a primitive acceleration structure whose primitive index N
  corresponds to `triangles[N]`, so a GPU hit resolves directly to that triangle

#### Scenario: Hit reports the source triangle index
- **WHEN** a ray intersects triangle N via the GPU intersector
- **THEN** the returned `Hit` SHALL reference triangle index N (the same index the CPU
  backend reports for well-separated geometry)

### Requirement: Runtime-compiled metal_raytracing kernels
The Metal backend SHALL use a `metal_raytracing` compute kernel
(`raytracing::intersector<triangle_data>` over `acceleration_structure<>`) to intersect a
buffer of rays and write hits, and SHALL use `accept_any_intersection(true)` for occlusion.
The kernel source SHALL be compiled at RUNTIME via `newLibraryWithSource:` (embedded as a
string) with no offline `.metallib` build step.

#### Scenario: Kernel compiles at runtime
- **WHEN** the Metal backend initializes on a device that supports ray tracing
- **THEN** it SHALL compile its embedded kernel source at runtime and SHALL surface a clear
  error (treating the backend as unavailable) rather than crashing if compilation fails

#### Scenario: Occlusion stops at first hit
- **WHEN** an occlusion query runs over a ray whose segment is blocked
- **THEN** the kernel SHALL report the ray as occluded using `accept_any_intersection(true)`
  without requiring the closest hit

### Requirement: Host and kernel buffer layout match
The Metal backend SHALL define matching ray and hit structs on the host and in the kernel
using `packed_float3` (or explicit float scalars) to avoid `float3` padding mismatches, and
SHALL use `MTLResourceStorageModeShared` buffers. The C++ `Vec3`/`Hit` types SHALL remain
double precision; values SHALL convert to float only inside the Metal buffers.

#### Scenario: Packed layout avoids padding desync
- **WHEN** rays are written into the shared ray buffer and hits are read back
- **THEN** the host struct layout SHALL match the kernel struct byte-for-byte (packed), so
  ray fields and hit fields are interpreted correctly on both sides

#### Scenario: Precision is converted at the buffer boundary
- **WHEN** double-precision rays are dispatched and float hits are read back
- **THEN** the backend SHALL convert double to float when filling buffers and float to
  double when producing `Hit`, leaving the public C++ types double

### Requirement: Backend selection and runtime availability
`backendAvailable(Backend::Metal)` SHALL return true only when `RFTRACE_HAVE_METAL` is
defined AND a Metal device that supports ray tracing exists at runtime.
`makeBackend(Backend::Metal, allowFallback)` SHALL return the Metal backend when compiled
and available, and SHALL otherwise fall back to the CPU backend when `allowFallback` is
true or throw a clear error when it is false, preserving the existing fallback behaviour.

#### Scenario: Availability reflects a real device
- **WHEN** the build defines `RFTRACE_HAVE_METAL` but no ray-tracing-capable Metal device is
  present at runtime
- **THEN** `backendAvailable(Backend::Metal)` SHALL return false

#### Scenario: Fallback to CPU when Metal is unavailable
- **WHEN** `makeBackend(Backend::Metal, true)` is called in a build without Metal or without
  a GPU
- **THEN** it SHALL return the CPU backend rather than failing

#### Scenario: Explicit no-fallback errors clearly
- **WHEN** `makeBackend(Backend::Metal, false)` is called and Metal is unavailable
- **THEN** it SHALL throw a clear error and SHALL NOT crash

