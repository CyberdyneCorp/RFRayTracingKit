# accelerated-simulation Specification

## Purpose
Make the simulator produce identical results faster on multi-core CPUs, without changing physics or
outputs: an exact receiver capture spatial index (near-linear ray-launch capture), deterministic
`threadCount` parallelism over independent receivers/cells (disjoint output slots ⇒ schedule- and
thread-count-independent results, gated to a thread-safe backend), and backend reuse across runs on an
unchanged scene. All results are bit-for-bit identical to the serial per-call path.
## Requirements
### Requirement: Results-preserving CPU acceleration
The simulator's CPU-side acceleration (spatial-indexed capture, parallelism, backend reuse) SHALL
NOT change simulation outputs. For any scene and settings, `Simulator::run`, `runCoverage`, and
`runRoute` SHALL produce results identical to the pre-change implementation: same paths, same
ordering of paths within a receiver result, and the same aggregated power / path loss / delay spread
/ SINR / Doppler and coverage-cell values. Determinism SHALL be preserved: outputs SHALL NOT depend
on thread count or scheduling, and repeated runs SHALL be identical.

#### Scenario: Serial and parallel results are identical
- **WHEN** the same scene and settings are simulated with `threadCount = 1` and with
  `threadCount = N > 1`
- **THEN** every receiver / coverage cell / route sample result SHALL be identical, element by
  element (bit-for-bit)

#### Scenario: Golden results unchanged
- **WHEN** the accelerated implementation runs the existing golden/regression scenes
- **THEN** the results SHALL match the recorded golden values with no tolerance change

### Requirement: Receiver capture spatial index
The stochastic ray-launch capture SHALL use a spatial index over receiver (coverage-cell) positions
so that a ray segment is tested only against receivers whose capture neighborhood it can reach,
instead of scanning every receiver. The set of captured paths and their dedup/strongest-wins outcome
SHALL be identical to the exhaustive scan; the index only avoids testing receivers that could not
capture the segment.

#### Scenario: Indexed capture equals brute-force capture
- **WHEN** ray launch runs over a set of receivers with the spatial index enabled
- **THEN** each receiver's captured path list (contents, order, and per-signature strongest winner)
  SHALL equal the result of the exhaustive per-receiver scan

#### Scenario: Cost scales with local receiver density
- **WHEN** the receiver count grows (e.g. a finer coverage grid) with fixed geometry
- **THEN** capture work SHALL scale with the receivers near each segment, not the total receiver
  count (no full `rays × receivers` scan)

### Requirement: Deterministic parallel execution across independent work
The simulator SHALL parallelize its independent per-receiver / per-cell work across a configurable
number of threads while writing each result into a disjoint slot, so no cross-receiver floating-point
accumulation occurs and results are independent of scheduling. A `threadCount` setting on
`SimulationSettings` SHALL control this (additive and default-neutral): `threadCount = 1` SHALL be
exactly today's serial behavior; the default SHALL use available hardware concurrency. Parallel
execution SHALL respect backend thread-safety: device (GPU) dispatch SHALL remain serial (GPU
backends are not reentrant), with parallelism applied to CPU-side path building, capture, and
aggregation (or gated to a thread-safe backend), never issuing concurrent queries to a single
non-reentrant backend instance.

#### Scenario: threadCount default runs in parallel
- **WHEN** a run is started with default settings on a multi-core host
- **THEN** independent receivers/cells SHALL be evaluated concurrently, and the result SHALL equal
  the serial result

#### Scenario: threadCount = 1 is exact serial
- **WHEN** `threadCount = 1`
- **THEN** the execution SHALL match the pre-change serial path and results bit-for-bit

#### Scenario: A single GPU backend is not queried concurrently
- **WHEN** a run uses a GPU backend
- **THEN** the simulator SHALL NOT issue concurrent queries to that single backend instance; device
  dispatch SHALL be serialized while CPU-side work may still be parallelized

### Requirement: Backend reuse across runs
The simulator SHALL support reusing a built backend acceleration structure across runs on the same
scene, so repeated `run` / `runCoverage` / `runRoute` invocations do not rebuild the structure each
time. Reuse SHALL be safe (invalidated when the scene geometry changes) and SHALL NOT change results
versus building per call.

#### Scenario: Repeated runs on one scene skip the rebuild
- **WHEN** multiple simulations run against the same unchanged scene through a reusing entry point
- **THEN** the backend acceleration structure SHALL be built once and reused, and each run's result
  SHALL equal the result of building the backend per call

#### Scenario: Scene change invalidates the cached backend
- **WHEN** the scene geometry changes between runs
- **THEN** the cached backend SHALL be rebuilt so results reflect the new geometry

