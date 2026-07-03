## 1. Groundwork & safety net

- [x] 1.1 Add a batched-vs-per-ray equality test harness: run `Simulator::run`, `runCoverage`, and `runRoute` on representative scenes (CPU backend) and snapshot outputs to compare against after each phase.
- [x] 1.2 Confirm the existing golden/regression suites (`test_golden`, `test_phase2_golden`, coverage, route) run green as the bit-for-bit gate; note which ones cover the touched paths.
- [x] 1.3 Add an internal `detail::BatchQuery` helper (header-only, `src/detail/` or `include/rftrace/detail/`) that owns reusable ray + `Hit`/`char` result buffers and exposes gather → `closestHitBatchInto`/`occludedBatchInto` → indexed-scatter plumbing.
- [x] 1.4 Unit-test the helper: gathered order preserved, results index-aligned, buffers reused across calls, empty batch handled.

## 2. Phase 1 — LOS occlusion + coverage grid

- [x] 2.1 Refactor `Simulator::run` LOS loop to gather all (receiver × transmitter) occlusion rays, dispatch one `occludedBatchInto`, then scatter into per-receiver LOS/diffraction decisions in the same order.
- [x] 2.2 Refactor `fillCoverageImageMethod` and `fillCoverageMultipath` LOS to batch occlusion across all cells × transmitters.
- [x] 2.3 Keep the vegetation-enabled path (`blockedByNonVegetation`) correct: batch only the plain-`occluded` case in this phase; leave the vegetation walk per-ray (D5).
- [x] 2.4 Verify: golden suites green; batched-vs-per-ray equality test passes for LOS-dominated scenes; determinism (repeat-run equality) holds.

## 3. Phase 2 — Ray-launch wavefront batching

- [x] 3.1 Restructure `rayLaunch` from per-ray/all-bounces to per-bounce/all-live-rays: maintain a live-ray set, batch `closestHit` for all live rays at each bounce, scatter hits, advance/terminate rays, iterate to `maxReflections`.
- [x] 3.2 Preserve RNG draw order exactly (generate all initial ray directions up front as today) and capture-sphere accumulation order so paths and keys are unchanged (D4).
- [x] 3.3 Verify: ray-launch golden/coverage-multipath tests green; batched-vs-per-ray equality on ray-launch scenes; repeat-run determinism holds.

## 4. Phase 3 — Image-method, diffraction, and terrain probes — DEFERRED

DEFERRED out of this change. Rationale (from the Phase-5 end-to-end benchmark): once LOS and
ray-launch traversal are batched, a full coverage run's GPU traversal is already a handful of
dispatches (~1 ms), and full-run wall time is dominated by CPU-side path processing (capture/dedup,
RF physics, aggregation) — not ray traversal. Batching these remaining per-ray sites is correct and
worthwhile for traversal-heavy scenes, but will not move full-run wall time while CPU work dominates
(and image-method's O(n^depth) enumeration is pure CPU). Tracked as a known gap in project.md; the
higher-value follow-up is CPU-side full-run acceleration (threading, capture spatial index, backend
reuse), to be scoped as its own change.

- [ ] 4.1 Batch image-method reflection-segment occlusion: gather the segments of a candidate path (and across candidates at a fixed depth) into one `occludedBatchInto`, preserving candidate/path ordering. (deferred)
- [ ] 4.2 Batch `buildTerrainProfile`'s 128 downward `closestHit` probes per link (and across links where a stage evaluates many). (deferred)
- [ ] 4.3 Batch `evaluateEdge` diffraction occlusion (2 rays/edge) across all boundary edges of a link. (deferred)
- [ ] 4.4 Verify: diffraction/UTD/multi-edge and image-method golden tests green; equality test passes; determinism holds. (deferred)

## 5. GPU acceleration validation & docs

- [x] 5.1 Add a GPU-gated test: `runCoverage` on the CUDA backend agrees with the CPU result within the CPU-vs-GPU parity tolerance (skips when no device). — `CudaFullSim.LosCoverageAgreesWithCpu` in test_cuda_parity.cpp.
- [x] 5.2 Benchmark a representative coverage/route scene CPU vs CUDA end-to-end (extend the example or add a small harness) and record the full-run speedup. — `rftrace_sim_benchmark` example. Finding: batched traversal collapses a whole coverage run to a handful of dispatches (~1 ms total), but full-run wall time is dominated by CPU-side capture/dedup/RF-physics (and per-run OptiX backend build), so end-to-end GPU speedup is ~1x here; the real full-run bottleneck is CPU path processing, not ray traversal.
- [x] 5.3 Confirm GPU runs issue batched dispatches, not per-ray calls (e.g. via the `RFTRACE_CUDA_PROFILE` dispatch counter / phase log). — Confirmed: a ray-launch coverage run issues 5 batched dispatches (LOS + per-bounce wavefront) totaling ~0.9 ms, not per-ray round trips.
- [x] 5.4 Update README (GPU-backend note: simulator now issues batched queries; full-run bottleneck is CPU-side) and `openspec/project.md` (batched simulator path moved from known gaps to done, with the Phase-3 deferral + CPU-side follow-up noted); run `openspec validate --strict` and archive the change.
