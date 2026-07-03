## Why

The batched simulator path (archived) removed per-ray host↔device round trips, but the end-to-end
benchmark showed full-run wall time is now dominated by **CPU-side work, not ray traversal**: a
ray-launch coverage run's GPU traversal is ~1 ms of batched dispatches, yet the run takes seconds.
The costs are (1) the ray-launch capture loop is **O(rays × receivers)** — each ray segment is tested
against *every* receiver/cell (billions of `distancePointToSegmentSq` calls on a coverage grid,
~2.2 s in the benchmark) plus string-keyed signature dedup; (2) per-path RF physics and aggregation;
and (3) every `run`/`runCoverage`/`runRoute` **rebuilds the backend acceleration structure per call**
(OptiX context/GAS or BVH). These are where a full run actually spends its time.

## What Changes

Three CPU-side levers, all **results-preserving** (bit-for-bit identical to today, deterministic):

- **Capture spatial index** — build a uniform grid / hash over receiver (cell) positions so the
  ray-launch capture only tests receivers near each ray segment. Exact same captures (each receiver
  owns its `out[r]`, so skipping far receivers changes nothing); it just avoids the quadratic scan.
- **Deterministic multi-threading** of the independent receiver/cell loops (`Simulator::run`,
  `fillCoverageImageMethod`). Each receiver/cell computes its own paths + aggregation into a
  **disjoint result slot**, so results are bit-for-bit **regardless of thread count/scheduling**
  (thread count 1 == today's serial). Add a `threadCount` knob to `SimulationSettings` (additive,
  default = hardware concurrency; 1 preserves exact serial behavior). GPU backends are not reentrant,
  so device dispatch stays serial and parallelism targets the CPU-side path-building/capture/
  aggregation (or is gated to the CPU backend / one-backend-per-thread — see design).
- **Backend reuse across runs** — cache/persist the built backend keyed by scene so repeated runs
  (parameter sweeps, coverage + route on one scene) don't re-pay the acceleration-structure build.

RF physics, results, path ordering, and the FP-sum order in aggregation are unchanged.

## Capabilities

### New Capabilities
- `accelerated-simulation`: the simulator produces identical results faster on multi-core CPUs via a
  capture spatial index (exact), deterministic parallelism over independent receivers/cells, and
  backend reuse across runs — with a `threadCount` setting and no change to physics or results.

### Modified Capabilities
<!-- None. ray-simulation / stochastic-raylaunch / coverage-grid observable requirements are
     unchanged (results bit-for-bit identical, determinism preserved). The new threadCount setting
     and the acceleration mechanisms are captured by the new accelerated-simulation capability. -->

## Impact

- **Code**: `src/simulator.cpp` (parallelize `run` LOS-follow-up + reflection loops and
  `fillCoverageImageMethod`; wire backend reuse), `src/backends/cpu_nanort/raylaunch.cpp` (capture
  spatial index over receivers; optional per-ray parallel traversal/capture with deterministic
  merge), `include/rftrace/simulator.hpp` (`threadCount` in `SimulationSettings`; optional
  persistent-backend surface), a new internal receiver spatial-index helper, and a small thread-pool
  / parallel-for utility (or C++17 `<execution>` if available).
- **Public API**: additive only — a new default-neutral `SimulationSettings.threadCount`; existing
  signatures and results unchanged.
- **Backends**: none changed; parallelism respects the documented GPU single-thread contract.
- **Tests**: golden/regression suites are the bit-for-bit gate; add tests that results are identical
  across `threadCount ∈ {1, N}`, that the spatial-index capture equals the brute-force capture, and
  determinism across repeated runs. A CPU full-run benchmark records the speedup.
- **Risk**: threading a numeric pipeline (determinism/FP-order) and a hot capture rewrite — contained
  by per-receiver independence (no cross-receiver sums), an exact spatial index, and the golden gate.
