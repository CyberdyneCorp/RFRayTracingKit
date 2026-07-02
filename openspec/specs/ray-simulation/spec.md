# ray-simulation Specification

## Purpose
TBD - created by archiving change phase1-cpu-prototype. Update Purpose after archive.
## Requirements
### Requirement: Backend abstraction
The library SHALL define a backend interface that decouples ray traversal and bulk RF
computation from the simulation model, so that additional backends (Metal, CUDA, OpenCL)
can be added without changing scene, RF physics, or result code.

#### Scenario: Backend selection via settings
- **WHEN** `SimulationSettings.backend` names an available backend
- **THEN** the simulator SHALL run using that backend

#### Scenario: Unavailable backend falls back or errors
- **WHEN** a backend is requested that was not compiled in
- **THEN** the simulator SHALL either fall back to the CPU backend or report a clear error,
  as documented, and SHALL NOT crash

### Requirement: CPU backend
The library SHALL provide a CPU backend built on the NanoRT-style BVH that serves as the
correctness reference for all other backends.

#### Scenario: CPU backend runs a simulation
- **WHEN** a scene is simulated with `Backend::CPU`
- **THEN** the backend SHALL build the BVH, perform ray/occlusion queries, and produce RF
  paths and per-receiver results

### Requirement: Simulation settings
The library SHALL expose `SimulationSettings` controlling at least the backend, maximum
reflection bounces (`maxReflections`), rays per transmitter, and the receiver capture
radius, with documented defaults.

#### Scenario: Default settings are valid
- **WHEN** `SimulationSettings` is default-constructed
- **THEN** it SHALL produce a runnable configuration (CPU backend, a finite
  `maxReflections`, and a positive capture radius)

#### Scenario: maxReflections bounds bounce depth
- **WHEN** `maxReflections` is set to N
- **THEN** the simulator SHALL not generate reflection paths with more than N bounces

### Requirement: Point-receiver simulation mode
The library SHALL support a point-receiver mode that computes propagation from each
transmitter to each explicitly defined receiver.

#### Scenario: One transmitter to many receivers
- **WHEN** a scene has one transmitter and several receivers
- **THEN** the simulator SHALL produce results for every (transmitter, receiver) pair whose
  receiver is reached by at least one path

#### Scenario: Receiver with no reaching path
- **WHEN** a receiver is fully blocked from a transmitter (no LOS and no valid reflection)
- **THEN** the result SHALL record that receiver as having no received signal rather than
  omitting it silently

### Requirement: Line-of-sight path finding
The library SHALL determine line-of-sight visibility between a transmitter and a receiver
using an occlusion query, and produce a direct path when unobstructed.

#### Scenario: Unobstructed LOS produces a direct path
- **WHEN** the segment between transmitter and receiver is unoccluded
- **THEN** the simulator SHALL produce a LOS path with two points and FSPL-based received
  power

#### Scenario: Obstructed LOS produces no direct path
- **WHEN** geometry blocks the transmitter–receiver segment
- **THEN** the simulator SHALL not produce a LOS path for that pair

### Requirement: Specular reflection path finding
The library SHALL find specular reflection paths up to `maxReflections` bounces using the
image (mirror) method, validating each candidate path with occlusion queries for every
segment and confirming each reflection point lies on its reflecting triangle.

#### Scenario: Single-wall reflection is found
- **WHEN** a transmitter and receiver face a planar wall with a valid geometric reflection
- **THEN** the simulator SHALL produce a reflection path whose bounce point lies on the wall
  and whose segments are all unoccluded

#### Scenario: Invalid image point is rejected
- **WHEN** the mirror-image construction yields a reflection point outside the reflecting
  triangle, or any segment is occluded
- **THEN** that candidate reflection path SHALL be discarded

#### Scenario: Bounce count respected
- **WHEN** `maxReflections` is 1
- **THEN** only LOS and single-bounce paths SHALL be generated, and no two-bounce paths

### Requirement: Reproducible results
The library SHALL produce deterministic results for a fixed scene and fixed settings so
outputs are reproducible and comparable across runs and backends.

#### Scenario: Repeated runs match
- **WHEN** the same scene is simulated twice with identical settings on the CPU backend
- **THEN** the produced paths and per-receiver metrics SHALL be identical within
  floating-point tolerance

### Requirement: Propagation mode selection
The library SHALL let `SimulationSettings` select the propagation method: the deterministic
image method (Phase 1 default) or stochastic ray launch.

#### Scenario: Image method selected
- **WHEN** the propagation mode is set to image-method
- **THEN** the simulator SHALL produce paths via the deterministic image method as in
  Phase 1

#### Scenario: Ray-launch method selected
- **WHEN** the propagation mode is set to ray-launch
- **THEN** the simulator SHALL produce paths via the stochastic ray-launch engine with the
  receiver capture sphere

### Requirement: First-class multi-bounce reflections
The library SHALL support and validate reflection depth greater than one bounce in both
propagation modes, bounded by `maxReflections`.

#### Scenario: Two-bounce path is found
- **WHEN** a scene admits a valid two-bounce specular path and `maxReflections` ≥ 2
- **THEN** the simulator SHALL produce a path with two reflection points and reflection
  count 2

#### Scenario: Depth is still bounded
- **WHEN** `maxReflections` is N
- **THEN** no produced path SHALL exceed N reflections in either mode

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

### Requirement: Diffraction and advanced-loss simulation settings
`SimulationSettings` SHALL expose `enableDiffraction`, and toggles/parameters for atmospheric
(rain rate), vegetation, and SINR/noise-floor behavior, all defaulting off so Phase 1/2/4
results are unchanged.

#### Scenario: Defaults preserve prior behavior
- **WHEN** `SimulationSettings` is default-constructed
- **THEN** diffraction, rain, vegetation, and SINR features SHALL be disabled and results
  SHALL match the archived Phase 1/2 behavior

### Requirement: Route simulation mode
The simulator SHALL support a route (moving-receiver) mode that evaluates a receiver route and
returns an ordered per-sample result series.

#### Scenario: Route mode returns a series
- **WHEN** the simulator is run over a defined route
- **THEN** it SHALL return one result per route sample, in order

### Requirement: CPU-vs-OpenCL traversal parity
When the OpenCL backend is available, its ray-traversal results SHALL match the CPU reference backend
for the same scene and rays: hit vs miss SHALL match exactly, the reported triangle index SHALL be
the same for well-separated geometry, and the hit distance `t` SHALL match within tolerance
`|dt| <= max(1e-2 m, 1e-4*|t|)`, accounting for OpenCL's float32 versus the CPU's double precision
(the same rule as the Metal parity tests).

#### Scenario: Closest-hit parity on well-separated geometry
- **WHEN** identical ray batches are run through the CPU and OpenCL backends over a scene of
  well-separated triangles
- **THEN** each ray SHALL agree on hit/miss and, when hit, on the triangle index, with `t` within the
  documented tolerance

#### Scenario: Occlusion parity
- **WHEN** identical segments are queried via `occludedBatch` on both backends over blocked and clear
  paths
- **THEN** the occluded/clear result SHALL match for every segment

### Requirement: OpenCL parity tests skip gracefully without a device
The OpenCL parity/behaviour tests SHALL build and run only under `RFTRACE_ENABLE_OPENCL` (guarded by
`#if RFTRACE_HAVE_OPENCL`) and SHALL skip at runtime when `backendAvailable(Backend::OpenCL)` is
false, so the default (non-OpenCL) build and CI remain unaffected. On a host with an OpenCL device
(e.g. the Apple M2 Max) these tests actually execute on the GPU.

#### Scenario: Skip when no device is present
- **WHEN** the OpenCL tests run in a `RFTRACE_HAVE_OPENCL` build but no OpenCL device is available
- **THEN** the tests SHALL call `GTEST_SKIP()` rather than fail

#### Scenario: Default build excludes OpenCL tests
- **WHEN** the project is built without `RFTRACE_ENABLE_OPENCL`
- **THEN** the OpenCL test file SHALL be compiled out and the existing C++ test suite (125 tests)
  SHALL build and pass unchanged

### Requirement: CPU-vs-CUDA traversal parity
When the CUDA/OptiX backend is available, its ray-traversal results SHALL match the CPU reference
backend for the same scene and rays: hit vs miss SHALL match exactly, the reported triangle index
SHALL be the same for well-separated geometry, and the hit distance `t` SHALL match within tolerance
`|dt| <= max(1e-2 m, 1e-4*|t|)`, accounting for CUDA's float32 versus the CPU's double precision (the
same rule as the Metal parity tests).

#### Scenario: Closest-hit parity on well-separated geometry
- **WHEN** identical ray batches are run through the CPU and CUDA backends over a scene of
  well-separated triangles
- **THEN** each ray SHALL agree on hit/miss and, when hit, on the triangle index, with `t` within the
  documented tolerance

#### Scenario: Occlusion parity
- **WHEN** identical segments are queried via `occludedBatch` on both backends over blocked and clear
  paths
- **THEN** the occluded/clear result SHALL match for every segment

### Requirement: CUDA parity tests skip gracefully without a GPU
The CUDA parity/behaviour tests SHALL build and run only under `RFTRACE_ENABLE_CUDA` (guarded by
`#if RFTRACE_HAVE_CUDA`) and SHALL skip at runtime when `backendAvailable(Backend::CUDA)` is false, so
the default (non-CUDA) build and CI remain unaffected. These tests are UNVERIFIED on non-NVIDIA hosts
and are to be validated on NVIDIA hardware.

#### Scenario: Skip when no GPU is present
- **WHEN** the CUDA tests run in a `RFTRACE_HAVE_CUDA` build but no CUDA device is available
- **THEN** the tests SHALL call `GTEST_SKIP()` rather than fail

#### Scenario: Default build excludes CUDA tests
- **WHEN** the project is built without `RFTRACE_ENABLE_CUDA`
- **THEN** the CUDA test file SHALL be compiled out and the existing C++ test suite (125 tests) SHALL
  build and pass unchanged

