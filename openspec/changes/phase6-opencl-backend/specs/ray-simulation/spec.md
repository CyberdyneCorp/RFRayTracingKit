## ADDED Requirements

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
