## ADDED Requirements

### Requirement: Embree CPU traversal backend
The library SHALL provide an Intel-Embree CPU backend implementing the `IBackend` interface, compiled
only when `RFTRACE_ENABLE_EMBREE=ON` AND Embree 4 is found (`RFTRACE_HAVE_EMBREE`). The backend SHALL
accelerate ray traversal only and SHALL NOT change RF physics, the scene model, the simulator, or
result formats. It SHALL implement `build`, `closestHit`, `occluded`, and the batched query methods,
and report `kind()` as `Backend::Embree`.

#### Scenario: Embree backend implements the contract
- **WHEN** the project is built with `RFTRACE_ENABLE_EMBREE=ON` (Embree 4 present) and an Embree
  backend is created
- **THEN** it SHALL implement `build`, `closestHit`, `occluded`, the batched methods, and report
  `kind()` as `Backend::Embree`

#### Scenario: Core is unchanged when Embree is off
- **WHEN** the project is built with `RFTRACE_ENABLE_EMBREE=OFF` (default)
- **THEN** the Embree backend SHALL NOT be compiled, `RFTRACE_HAVE_EMBREE` SHALL be undefined, and the
  existing library and test suite SHALL build and pass unchanged

### Requirement: Embree acceleration structure from scene triangles
The Embree backend SHALL build an `RTCScene` containing a single triangle geometry from the scene
triangles, using a float32 vertex buffer and an `unsigned int` index buffer such that the Embree
primitive index equals the corresponding `Triangle` index, and commit the scene before queries.

#### Scenario: Primitive index maps to triangle index
- **WHEN** `build(triangles)` is called on the Embree backend
- **THEN** it SHALL construct a committed `RTCScene` whose primitive id N corresponds to
  `triangles[N]`, so a hit resolves directly to that triangle

#### Scenario: Hit reports the source triangle index
- **WHEN** a ray intersects triangle N via the Embree intersector
- **THEN** the returned `Hit` SHALL reference triangle index N (the same index the reference CPU
  backend reports for well-separated geometry)

### Requirement: Precision boundary and query mapping
The public `Vec3`/`Hit` types SHALL remain double precision; values SHALL convert to float only inside
the Embree ray/geometry buffers. `closestHit` SHALL use Embree's closest-hit intersection
(`rtcIntersect1` or a packet variant) and `occluded` SHALL use Embree's occlusion test
(`rtcOccluded1` or a packet variant), honouring the ray's `[tMin, tMax]` interval.

#### Scenario: Precision converts at the buffer boundary
- **WHEN** double-precision rays are queried and float hits are read back
- **THEN** the backend SHALL convert double to float when filling the Embree ray and float to double
  when producing `Hit`, leaving the public C++ types double

#### Scenario: Occlusion honours the ray interval
- **WHEN** an occlusion query runs over a ray whose blocking geometry lies inside `[tMin, tMax]`
- **THEN** the ray SHALL be reported occluded; geometry outside the interval SHALL NOT occlude it

### Requirement: Embree backend selection and runtime availability
`backendAvailable(Backend::Embree)` SHALL return true only when `RFTRACE_HAVE_EMBREE` is defined AND an
Embree device is created successfully at runtime. `makeBackend(Backend::Embree, allowFallback)` SHALL
return the Embree backend when compiled and available, and SHALL otherwise fall back to the CPU
backend when `allowFallback` is true or throw a clear error when it is false — mirroring the other
optional backends.

#### Scenario: Availability reflects a real device
- **WHEN** the build defines `RFTRACE_HAVE_EMBREE` and an Embree device can be created
- **THEN** `backendAvailable(Backend::Embree)` SHALL return true; if device creation fails it SHALL
  return false

#### Scenario: Falls back to CPU when Embree is absent
- **WHEN** `makeBackend(Backend::Embree, true)` is called in a build without Embree
- **THEN** it SHALL return the reference CPU backend rather than failing

### Requirement: CPU-vs-Embree traversal parity
The Embree backend SHALL agree with the reference CPU BVH under the established float-vs-double parity
rule: hit/miss and triangle index match for well-separated geometry, hit distance within the D4
tolerance, and occlusion agreement; float-rounding disagreements at grazing/boundary/near-tie cases
are classified as borderline (verified against exact double geometry) rather than failures, mirroring
the CUDA/Metal/OpenCL parity suites. The comparison SHALL be deterministic across repeated runs.

#### Scenario: Closest-hit parity on well-separated geometry
- **WHEN** a batch of rays is traced against a scene with well-separated triangles on both the
  reference CPU backend and the Embree backend
- **THEN** hit/miss, triangle index, and hit distance (within tolerance) SHALL agree, with any
  disagreement confined to borderline cases

#### Scenario: Occlusion parity
- **WHEN** a batch of occlusion segments is evaluated on both backends
- **THEN** the occlusion results SHALL agree except at borderline (grazing/boundary) cases
