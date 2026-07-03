## Context

End-to-end profiling of the archived batched simulator path showed a ray-launch coverage run's GPU
traversal is ~1 ms (5 batched dispatches) while the run takes ~2.3 s — full-run time is CPU-bound.
The three CPU costs, from `src/simulator.cpp` and `src/backends/cpu_nanort/raylaunch.cpp`:

- **Quadratic capture**: `rayLaunch` captures by testing each ray segment against **every** receiver
  (`for r in receivers: distancePointToSegmentSq(...)`), i.e. `O(rays × bounces × receivers)`. On a
  coverage grid (tens of thousands of cells) this is billions of checks — the dominant cost.
- **Per-run backend build**: `Simulator::run/runCoverage/runRoute` each call `makeBackend(...)` +
  `backend->build(scene.triangles())`, rebuilding the BVH/OptiX-GAS every invocation.
- **Serial independent work**: `run`'s per-receiver reflection enumeration and
  `fillCoverageImageMethod`'s per-cell evaluation are independent but run on one thread.

Hard constraint: results **bit-for-bit identical**, deterministic, no physics/result/public-API
change. The batched-simulation capability (traversal batching) stays as-is underneath.

## Goals / Non-Goals

**Goals:**
- Cut the quadratic capture to near-linear with an exact receiver spatial index.
- Parallelize independent per-receiver/per-cell work deterministically (`threadCount` knob).
- Reuse the built backend across runs on an unchanged scene.
- Keep every output bit-for-bit identical; determinism independent of thread count/scheduling.

**Non-Goals:**
- No change to RF physics, aggregation math, path contents/ordering, or result formats.
- No GPU/backend changes; respect the documented GPU single-thread contract.
- Not parallelizing across bounces within one ray (dependent) or across transmitters in a way that
  reorders per-receiver accumulation.
- Not a distributed/multi-process path.

## Decisions

### D1 — Exact receiver spatial index for capture
Build a uniform grid (or hash grid) over receiver positions, cell size ≈ capture diameter, once per
`rayLaunch` call. For each ray segment, query only the grid cells the segment's capture tube
overlaps and test those receivers. **Exactness:** each receiver owns its `out[r]`; a receiver not
near the segment could never capture it, so skipping it changes no `out[r]`. Among the receivers that
*are* tested, keep the original receiver-index iteration order so per-`out[r]` insertion / dedup is
unchanged (it already is — dedup is per receiver). *Alternatives:* a BVH over receivers (overkill for
points); sorting rays by cell (changes capture order across receivers — avoid).

### D2 — Parallelism over independent receivers/cells, disjoint output slots
Parallelize the outer loops whose iterations are fully independent and write to distinct slots:
`fillCoverageImageMethod` (per cell → `cov` arrays), and `Simulator::run`'s per-receiver reflection
collection (`rrs[i]`). Each task builds its own `ReceiverResult` and aggregates it; **no cross-task
accumulation**, so the FP-sum order inside each receiver is identical to serial and the result is
independent of scheduling. Use a small thread pool / parallel-for over an index range with a fixed,
deterministic partition (results written by index, not append order). *Alternative:* C++17
`std::execution::par` — viable if the toolchain/stdlib supports it (TBB); fall back to an in-repo
thread pool otherwise.

### D3 — Backend thread-safety: serialize device dispatch, parallelize CPU-side work
The CPU BVH's `closestHit`/`occluded` are const and safe for concurrent reads, so with the CPU
backend the independent loops can call it directly from many threads. GPU backends are **not
reentrant** (one stream + pooled buffers). Rule: **never issue concurrent queries to a single backend
instance.** Options, in order of preference: (a) when the backend is CPU, parallelize freely; (b) when
GPU, keep the (already batched) device dispatch serial and parallelize only the CPU-side capture /
path-building / aggregation around it; (c) if needed, one backend instance per worker thread. Phase 1
targets the CPU backend (where the win is largest and safe); GPU parallel-CPU-side is a refinement.

### D4 — `threadCount` setting, default-neutral
Add `int threadCount` to `SimulationSettings` (0 or negative → hardware concurrency; 1 → serial).
`threadCount = 1` MUST reproduce today's code path exactly (ideally the same serial loop, not a
1-thread pool, to guarantee bit-for-bit and easy diffing). Default preserves results but uses cores.

### D5 — Backend reuse without changing per-call semantics
Two options: (a) an internal cache in `Simulator` keyed by a scene fingerprint (triangle-buffer
identity/hash) that rebuilds on change; (b) an explicit persistent-backend entry point
(`Simulator::prepare(scene)` returning a reusable handle used by subsequent runs). Prefer (a) as an
internal optimization first (no API surface), guarded so a changed scene invalidates it; expose (b)
later if callers want explicit control. Results identical either way (same built structure).

### D6 — Equality is enforced by tests
Golden/regression suites are the gate. Add: (1) `threadCount ∈ {1, N}` produce identical
`run`/`runCoverage`/`runRoute` outputs; (2) indexed capture == brute-force capture (drive `rayLaunch`
directly, compare `out[r]` exactly, reuse the Phase-2 differential-oracle style); (3) determinism
across repeated parallel runs; (4) scene-change invalidates the reused backend.

## Risks / Trade-offs

- **[Threading breaks determinism / FP-order]** → parallelize only across independent receivers/cells
  with disjoint slots (no shared reduction); `threadCount=1` is the exact serial path; equality tests
  across thread counts gate every step.
- **[Concurrent queries to a non-reentrant GPU backend]** → D3: never share one backend across
  concurrent queries; CPU-backend-first, device dispatch serial.
- **[Spatial index subtly drops/reorders a capture]** → D1 keeps receiver-index order among tested
  receivers and only prunes provably-out-of-range ones; brute-force-equality test guards it.
- **[Backend-reuse staleness]** → invalidate on scene fingerprint change; test scene-change rebuild.
- **[Thread-pool dependency/portability]** → prefer a tiny in-repo pool over adding TBB; keep it
  header-only and `detail`.

## Migration Plan

- Land as sequential PRs, each golden-gated and independently revertible:
  1. Capture spatial index (biggest ray-launch win, exact, no threading).
  2. Deterministic parallelism + `threadCount` (CPU backend first).
  3. Backend reuse.
- No flag needed for correctness (results identical); `threadCount` defaults to parallel but is
  provably equal to serial. A CPU full-run benchmark records the speedup per phase.

## Open Questions

- In-repo thread pool vs `std::execution::par` (TBB availability in the vcpkg toolchain)?
- Backend reuse as an internal `Simulator` cache (no API) vs an explicit `prepare()` handle — start
  internal, revisit if callers need control.
- For GPU backends, is parallel CPU-side-around-serial-dispatch worth it, or is CPU-backend
  parallelism plus backend reuse enough for the target workloads?
