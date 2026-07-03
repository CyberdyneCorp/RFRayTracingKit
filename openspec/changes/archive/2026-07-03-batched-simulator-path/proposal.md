## Why

The GPU traversal backends (CUDA, Metal, OpenCL) are fast per batch, but the simulator still
drives them one ray at a time via `IBackend::closestHit`/`occluded`. Each such call is a
host↔device round trip, so a full `run`/`runCoverage`/`runRoute` gets no GPU speedup today —
selecting a GPU backend is correct but no faster than CPU. The zero-allocation caller-owned-output
API (`closestHitBatchInto`/`occludedBatchInto`) now exists; this change puts it to work so a whole
simulation run dispatches rays in batches.

## What Changes

- Restructure the simulator's hot query paths from per-ray calls into **gather → batched dispatch
  → scatter**: collect the independent rays for a stage into one buffer, issue a single
  `closestHitBatchInto`/`occludedBatchInto`, then consume the results in order.
- Add a small internal **batch-query helper** (gather/scatter plumbing + reusable buffers) so the
  hot loops stay readable and every stage shares one code path.
- Phase the rollout by ray-consumer, biggest and most-parallel win first:
  1. **LOS occlusion** across all receivers / coverage cells (one `occluded()` per tx×rx today).
  2. **Coverage grid** loops (`runCoverage` image-method and ray-launch cell evaluation).
  3. **Ray-launch wavefront** (`rayLaunch`): batch `closestHit` across live rays per bounce.
  4. **Image-method reflection-segment** occlusion and **terrain-profile** down-rays
     (`buildTerrainProfile`, 128/link) and **diffraction boundary-edge** occlusion (2/edge).
- **No physics/result change**: outputs SHALL stay bit-for-bit identical to the current per-ray
  path, and the CPU backend SHALL be unaffected (the batched primitive already loops single-ray
  for it). Determinism (fixed RNG streams, path ordering) is preserved.

## Capabilities

### New Capabilities
- `batched-simulation`: the simulator issues batched, caller-owned-output backend queries for its
  independent-ray stages (LOS, coverage, ray-launch wavefront, reflection/diffraction/terrain
  probes), producing results identical to the per-ray path so GPU backends accelerate a full run.

### Modified Capabilities
<!-- None: observable requirements of ray-simulation / coverage-grid / stochastic-raylaunch are
     unchanged (results and determinism identical). This change adds an internal how-it-dispatches
     requirement captured by the new batched-simulation capability, and builds on the already-
     specified caller-owned-output batched query API in ray-simulation. -->

## Impact

- **Code**: `src/simulator.cpp` (LOS, coverage, image-method, diffraction, terrain profile),
  `src/backends/cpu_nanort/raylaunch.cpp` (wavefront), `include/rftrace/detail/propagation.hpp`
  (`blockedByNonVegetation` and any per-ray helpers on the batched path); a new internal
  batch-query helper header.
- **Public API**: none changed. `Simulator::run`/`runCoverage`/`runRoute` signatures and results
  are unchanged; the batched query API they build on already exists on `IBackend`.
- **Backends**: no backend changes required — batching flows through the existing
  `closestHitBatchInto`/`occludedBatchInto`. GPU backends benefit; CPU is neutral.
- **Tests**: existing golden/regression suites must stay green (they are the bit-for-bit guard);
  add coverage that batched and per-ray paths agree, and (GPU-gated) that a coverage run on the
  CUDA backend matches the CPU result.
- **Risk**: behavior-preserving refactor of the most intricate simulator loops (recursive
  reflection enumeration, vegetation-aware occlusion, RNG-driven ray launch) — the phasing and the
  golden tests contain that risk.
