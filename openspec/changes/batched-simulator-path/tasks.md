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

- [ ] 3.1 Restructure `rayLaunch` from per-ray/all-bounces to per-bounce/all-live-rays: maintain a live-ray set, batch `closestHit` for all live rays at each bounce, scatter hits, advance/terminate rays, iterate to `maxReflections`.
- [ ] 3.2 Preserve RNG draw order exactly (generate all initial ray directions up front as today) and capture-sphere accumulation order so paths and keys are unchanged (D4).
- [ ] 3.3 Verify: ray-launch golden/coverage-multipath tests green; batched-vs-per-ray equality on ray-launch scenes; repeat-run determinism holds.

## 4. Phase 3 — Image-method, diffraction, and terrain probes

- [ ] 4.1 Batch image-method reflection-segment occlusion: gather the segments of a candidate path (and across candidates at a fixed depth) into one `occludedBatchInto`, preserving candidate/path ordering.
- [ ] 4.2 Batch `buildTerrainProfile`'s 128 downward `closestHit` probes per link (and across links where a stage evaluates many).
- [ ] 4.3 Batch `evaluateEdge` diffraction occlusion (2 rays/edge) across all boundary edges of a link.
- [ ] 4.4 Verify: diffraction/UTD/multi-edge and image-method golden tests green; equality test passes; determinism holds.

## 5. GPU acceleration validation & docs

- [ ] 5.1 Add a GPU-gated test: `runCoverage` on the CUDA backend agrees with the CPU result within the CPU-vs-GPU parity tolerance (skips when no device).
- [ ] 5.2 Benchmark a representative coverage/route scene CPU vs CUDA end-to-end (extend the example or add a small harness) and record the full-run speedup.
- [ ] 5.3 Confirm GPU runs issue batched dispatches, not per-ray calls (e.g. via the `RFTRACE_CUDA_PROFILE` dispatch counter / phase log).
- [ ] 5.4 Update README (GPU-backend note: full runs now accelerated) and `openspec/project.md` (move "batched simulator path" from known gaps to done); run `openspec validate --strict` and archive the change.
