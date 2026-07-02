## ADDED Requirements

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
