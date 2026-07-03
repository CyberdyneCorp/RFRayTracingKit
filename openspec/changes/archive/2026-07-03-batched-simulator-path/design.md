## Context

`Simulator::run`, `runCoverage`, and `runRoute` drive the backend through single-ray
`IBackend::closestHit`/`occluded`. On GPU backends every such call is a host↔device round trip, so
a full run is dominated by launch/transfer overhead and shows no speedup over CPU. The batched,
zero-allocation caller-owned-output API (`closestHitBatchInto(std::span<Hit>)` /
`occludedBatchInto(std::span<char>)`) now exists and its device fast paths are verified.

Current per-ray hotspots (from `src/simulator.cpp`, `src/backends/cpu_nanort/raylaunch.cpp`,
`include/rftrace/detail/propagation.hpp`):

- **LOS** (`losPath`): one `occluded()` per (tx, rx). In `runCoverage` this is
  `cells × transmitters` independent occlusion rays — the largest, most parallel pool.
- **Coverage** (`fillCoverageImageMethod` / `fillCoverageMultipath`): outer loops over every cell.
- **Ray-launch** (`rayLaunch`): each of `nRays` walks bounce-by-bounce via `closestHit`; the next
  segment starts at the previous hit — dependent along a walk, independent across walks.
- **Image-method reflections** (`buildReflection`): occlusion per candidate-path segment; candidate
  sequences enumerated recursively (`enumerateReflections`).
- **Terrain profile** (`buildTerrainProfile`): 128 downward `closestHit` probes per link.
- **Diffraction** (`evaluateEdge`): 2 `occluded()` per boundary edge per link.
- **Vegetation** (`blockedByNonVegetation`): walks `closestHit` along a segment skipping vegetation.

Constraint from the spec: results must be **bit-for-bit identical** and determinism preserved; the
CPU backend must stay neutral; no backend or public-API change.

## Goals / Non-Goals

**Goals:**
- Route the simulator's independent-ray stages through `...BatchInto`, gather→dispatch→scatter.
- Keep outputs identical to the per-ray path (golden suites are the gate) and preserve RNG streams
  and path ordering.
- A single reusable batch-query helper so the hot loops stay readable and share one code path.
- Deliver in independently-mergeable phases, biggest win first (LOS + coverage).

**Non-Goals:**
- No change to RF physics, aggregation, scene, results, or public signatures.
- No new GPU kernels or backend changes; batching flows through the existing interface.
- Not converting genuinely dependent, data-driven traversal into speculative batches beyond the
  per-wavefront model (e.g. we do not pre-expand the full reflection tree if it changes results).
- Not a multi-threaded CPU path (orthogonal; can come later).

## Decisions

### D1 — Gather/scatter with index-aligned buffers, not callbacks
Each stage builds a `std::vector<Ray>` of independent queries plus a parallel array of "sinks"
(where each result goes: which receiver/cell/candidate/bounce). Dispatch once into a reused
`std::vector<Hit>`/`std::vector<char>`, then a scatter loop consumes results in the same index
order. **Why:** index-aligned buffers keep ordering deterministic and make the batched result trivially
equal to the per-ray sequence. *Alternative considered:* a callback/visitor per ray — rejected, it
reintroduces per-ray indirection and obscures ordering.

### D2 — A small `BatchQuery` helper owning reusable buffers
Add an internal helper (e.g. `src/detail/batch_query.hpp`) holding the reused ray/result buffers and
thin `addOcclusion(ray)→token` / `runOcclusion(backend)` / `result(token)` style plumbing, or the
simpler explicit form `gather vector → backend.occludedBatchInto(rays, out)`. **Why:** one place for
buffer reuse and span wiring; hot loops read as gather/scatter. Keep it header-only, `detail`, no
public surface.

### D3 — Phase order (each phase independently correct, mergeable, golden-gated)
1. **LOS + coverage** — highest ray count, fully independent. Replace the per-cell/per-rx LOS
   occlusion with one batch per (all cells × tx); wire both `runCoverage` fills and `run`.
2. **Ray-launch wavefront** — restructure `rayLaunch` from "per ray, all bounces" to
   "per bounce, all live rays": maintain a live set, at each bounce batch `closestHit` for all live
   rays, scatter hits, advance/kill rays, repeat to `maxReflections`. Capture-sphere accumulation
   and RNG draw order must be preserved (draw all ray directions up front, as today).
3. **Image-method + diffraction + terrain** — batch reflection-segment occlusion across a
   candidate's segments and across candidates at a fixed depth; batch the 128 terrain down-rays per
   link and across links; batch the 2-per-edge diffraction occlusion across edges.

### D4 — Preserve RNG and ordering exactly
Ray-launch determinism depends on the RNG draw order. The wavefront restructuring SHALL draw the
same random numbers in the same order (generate all initial ray directions up front, unchanged) and
only reorganize when traversal happens, not what is traversed. Path collection order per receiver
SHALL match the current nested loops so aggregation and any order-sensitive output are identical.

### D5 — Vegetation and dependent walks stay per-ray within a batched frame where needed
`blockedByNonVegetation` walks a segment following hits; it is dependent. Phase 1 batches the LOS
call only when vegetation is off (the common path uses plain `occluded`), and leaves the vegetation
walk per-ray initially; a later refinement can batch the first probe across links. This keeps the
first phase simple and safe.

### D6 — Equality is enforced by tests, not by inspection
Add a batched-vs-reference equality test (CPU) comparing full `run`/`runCoverage`/`runRoute` outputs
before/after per stage, plus a GPU-gated `runCoverage` CPU-vs-CUDA agreement test. The existing
golden tests (`test_golden`, `test_phase2_golden`, coverage/route regressions) must stay green
unchanged.

## Risks / Trade-offs

- **[Behavior drift in intricate loops]** (recursive reflection enumeration, vegetation walk,
  RNG-driven launch) → Phase the change; each phase is golden-gated and independently revertible;
  add explicit batched-vs-per-ray equality tests before merging each phase.
- **[RNG order change silently alters ray-launch results]** → Keep direction generation up front and
  unchanged; assert reproducibility and equality to the pre-change output in tests.
- **[Batching too aggressively across dependent rays]** → Only batch within a wavefront/independent
  set (D1/D3); dependent steps stay ordered.
- **[Memory spikes on huge coverage grids]** (one big ray buffer) → buffers are reused and sized to
  the stage; if needed, chunk very large batches (still deterministic per chunk). Note any cap with
  a log rather than silently truncating.
- **[Little CPU benefit / slight overhead]** → Accepted: the goal is GPU acceleration; CPU stays
  neutral (batched primitive loops single-ray). Gather/scatter overhead is negligible vs traversal.

## Migration Plan

- Land as sequential PRs by phase (D3), each behind the golden suites; no flag needed because the
  behavior is identical. If a phase regresses results, revert that PR — earlier phases are
  independent.
- Optional interim safety: a build/CI job that runs a coverage/route scene on both CPU and CUDA and
  diffs the outputs.

## Open Questions

- Should the `BatchQuery` helper be a typed token API (D2 first form) or plain explicit
  gather-vectors (D2 second form)? Lean explicit for the first phase; promote to a helper if the
  scatter bookkeeping repeats.
- Chunk size for very large coverage grids (if a cap is introduced) — pick empirically on GPU;
  default to "one batch" until a real scene needs chunking.
- Whether to batch the vegetation-aware first probe in a later phase (D5) or leave it per-ray.
