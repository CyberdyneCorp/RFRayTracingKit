## ADDED Requirements

### Requirement: Batched ray-query API on the backend interface
The backend interface SHALL expose additive, non-pure batched query methods —
`closestHitBatch(const std::vector<Ray>&) const` returning `std::vector<Hit>` and
`occludedBatch(const std::vector<Ray>&) const` returning `std::vector<char>` — with default
implementations that loop the existing single-ray `closestHit`/`occluded`. The addition
SHALL be backward compatible: existing backends and callers continue to work without change,
and the CPU backend inherits the default implementations. `occludedBatch` SHALL return
`std::vector<char>` (not `std::vector<bool>`) so the result is a contiguous byte buffer.

#### Scenario: CPU backend inherits looping defaults
- **WHEN** `closestHitBatch(rays)` or `occludedBatch(rays)` is called on the CPU backend
- **THEN** the results SHALL be identical, element by element, to looping `closestHit`/
  `occluded` over the same rays

#### Scenario: GPU backend overrides with a single dispatch
- **WHEN** a batch of rays is submitted to the Metal backend's batched methods
- **THEN** the backend SHALL service the whole batch in a single GPU dispatch (not per-ray
  host<->GPU round-trips) and return one result per input ray, in input order

#### Scenario: Empty batch is handled
- **WHEN** an empty ray vector is passed to either batched method
- **THEN** the method SHALL return an empty result vector without error

### Requirement: CPU-vs-Metal traversal parity
When the Metal backend is available, its ray-traversal results SHALL match the CPU reference
backend for the same scene and rays: hit vs miss SHALL match exactly, the reported triangle
index SHALL be the same for well-separated geometry, and the hit distance `t` SHALL match
within tolerance (absolute ~1e-2 m and/or relative ~1e-4), accounting for Metal's float32
versus the CPU's double precision.

#### Scenario: Closest-hit parity on well-separated geometry
- **WHEN** identical ray batches are run through the CPU and Metal backends over a scene of
  well-separated triangles
- **THEN** each ray SHALL agree on hit/miss and, when hit, on the triangle index, with `t`
  within the documented tolerance

#### Scenario: Occlusion parity
- **WHEN** identical segments are queried via `occludedBatch` on both backends over blocked
  and clear paths
- **THEN** the occluded/clear result SHALL match for every segment

### Requirement: Metal parity tests skip gracefully without a GPU
The Metal parity/behaviour tests SHALL build and run only under `RFTRACE_ENABLE_METAL`
(guarded by `#if RFTRACE_HAVE_METAL`) and SHALL skip at runtime when
`backendAvailable(Backend::Metal)` is false, so the default (non-Metal) build and CI remain
unaffected.

#### Scenario: Skip when no GPU is present
- **WHEN** the Metal tests run in a `RFTRACE_HAVE_METAL` build but no ray-tracing-capable
  device is available
- **THEN** the tests SHALL call `GTEST_SKIP()` rather than fail

#### Scenario: Default build excludes Metal tests
- **WHEN** the project is built without `RFTRACE_ENABLE_METAL`
- **THEN** the Metal test file SHALL be compiled out and the existing C++ test suite SHALL
  build and pass unchanged
