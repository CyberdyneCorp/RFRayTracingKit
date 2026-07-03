## 1. Groundwork & safety net

- [ ] 1.1 Add a full-run equality harness: snapshot `run`, `runCoverage`, `runRoute` outputs on representative scenes (CPU backend) as the bit-for-bit reference to compare against after each phase.
- [ ] 1.2 Confirm the golden/regression suites covering ray-launch, coverage, image-method, and route are green as the gate; note which cover the touched paths.
- [ ] 1.3 Add a CPU full-run benchmark (extend `rftrace_sim_benchmark` or add a harness) recording per-phase wall time so each optimization's win is measured.

## 2. Phase 1 — Receiver capture spatial index

- [ ] 2.1 Add an internal uniform-grid / hash spatial index over receiver positions (header-only, `detail`), cell size ≈ capture diameter, built once per `rayLaunch` call.
- [ ] 2.2 Rewrite the `rayLaunch` capture loop to query only receivers in the segment's capture neighborhood, preserving receiver-index order among tested receivers and the exact dedup/strongest-wins per `out[r]`.
- [ ] 2.3 Test: indexed capture == exhaustive scan (drive `rayLaunch` directly, exact `==` on every `out[r]` across scenes × seeds × capture radii), plus determinism; golden ray-launch/coverage-multipath green.

## 3. Phase 2 — Deterministic parallelism + threadCount

- [x] 3.1 Add `int threadCount` to `SimulationSettings` (additive; 0/negative → hardware concurrency; 1 → exact serial path). Only effective on the CPU backend.
- [x] 3.2 Add a tiny in-repo parallel-for utility (`include/rftrace/detail/parallel_for.hpp`, header-only, `detail`) with a fixed deterministic contiguous index partition (no work-stealing); `threadCount<=1`/`n==0` take the exact serial ascending-index loop.
- [x] 3.3 Parallelize `fillCoverageImageMethod` (per cell; running token cursor replaced by the equal index-derived offset `i*txs.size()`) and `Simulator::run` per-receiver reflection collection, each writing a disjoint result slot; no cross-task accumulation. Ray-launch / `fillCoverageMultipath` left serial.
- [x] 3.4 Respect backend thread-safety (D3): parallelism gated to `backend.kind()==Backend::CPU` (const `occluded`/`closestHit` reads, safe concurrently); every non-CPU backend forces `tc=1` (serial), so a non-reentrant backend is never issued concurrent queries.
- [x] 3.5 Test (`tests/test_phase2_threading.cpp`): `run()` and `runCoverage()` bit-for-bit identical for `threadCount ∈ {1, 0(hw), explicit hw}` (NaN-safe bit compare over coverage arrays + per-path fields); repeated parallel runs deterministic; full golden suite green (242 tests). Measured ~10x CPU coverage speedup (24 cores) with identical output.

## 4. Phase 3 — Backend reuse across runs

- [x] 4.1 Add an internal `Simulator` backend cache keyed by a scene fingerprint (FNV-1a/64 content hash over triangle bytes + count, so in-place edits invalidate); rebuild on change. Adds observational `backendRebuildCount()`.
- [x] 4.2 Route `run`/`runCoverage`/`runRoute` through `ensureBackend(scene)` so repeated runs on one scene skip the acceleration-structure rebuild.
- [x] 4.3 Test (`tests/test_phase3_backend_reuse.cpp`): repeated run/runCoverage/runRoute on one scene reuse the backend (count==1) with identical results; a triangle-count change AND an equal-count/different-coordinate change invalidate and rebuild; reused output matches a fresh per-call build bit-for-bit. Full suite green (252 tests).

## 5. Validation & docs

- [ ] 5.1 Record the CPU full-run speedup per phase (spatial index, threading, reuse) on a representative coverage/route scene; note scaling with core count and receiver density.
- [ ] 5.2 Update README (simulator full-run acceleration; `threadCount` knob) and `openspec/project.md` (move CPU-side full-run acceleration from known gaps to done); run `openspec validate --strict` and archive the change.
