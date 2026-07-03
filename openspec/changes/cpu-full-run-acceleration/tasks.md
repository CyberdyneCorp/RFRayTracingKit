## 1. Groundwork & safety net

- [ ] 1.1 Add a full-run equality harness: snapshot `run`, `runCoverage`, `runRoute` outputs on representative scenes (CPU backend) as the bit-for-bit reference to compare against after each phase.
- [ ] 1.2 Confirm the golden/regression suites covering ray-launch, coverage, image-method, and route are green as the gate; note which cover the touched paths.
- [ ] 1.3 Add a CPU full-run benchmark (extend `rftrace_sim_benchmark` or add a harness) recording per-phase wall time so each optimization's win is measured.

## 2. Phase 1 — Receiver capture spatial index

- [ ] 2.1 Add an internal uniform-grid / hash spatial index over receiver positions (header-only, `detail`), cell size ≈ capture diameter, built once per `rayLaunch` call.
- [ ] 2.2 Rewrite the `rayLaunch` capture loop to query only receivers in the segment's capture neighborhood, preserving receiver-index order among tested receivers and the exact dedup/strongest-wins per `out[r]`.
- [ ] 2.3 Test: indexed capture == exhaustive scan (drive `rayLaunch` directly, exact `==` on every `out[r]` across scenes × seeds × capture radii), plus determinism; golden ray-launch/coverage-multipath green.

## 3. Phase 2 — Deterministic parallelism + threadCount

- [ ] 3.1 Add `int threadCount` to `SimulationSettings` (additive; 0/negative → hardware concurrency; 1 → exact serial path).
- [ ] 3.2 Add a tiny in-repo parallel-for / thread-pool utility (header-only, `detail`) with a fixed deterministic index partition, or wire `std::execution::par` if the toolchain supports it.
- [ ] 3.3 Parallelize `fillCoverageImageMethod` (per cell) and `Simulator::run` per-receiver reflection collection, each writing a disjoint result slot; no cross-task accumulation.
- [ ] 3.4 Respect backend thread-safety (D3): CPU backend queried concurrently (const reads); for GPU backends keep device dispatch serial / do not issue concurrent queries to one instance.
- [ ] 3.5 Test: results identical for `threadCount ∈ {1, hardware}` on all entry points; repeated parallel runs deterministic; golden suites green.

## 4. Phase 3 — Backend reuse across runs

- [ ] 4.1 Add an internal `Simulator` backend cache keyed by a scene fingerprint (triangle-buffer identity/hash); rebuild on change.
- [ ] 4.2 Route `run`/`runCoverage`/`runRoute` through the cache so repeated runs on one scene skip the acceleration-structure rebuild.
- [ ] 4.3 Test: repeated runs on one scene reuse the backend (build once) with identical results; a scene change invalidates and rebuilds.

## 5. Validation & docs

- [ ] 5.1 Record the CPU full-run speedup per phase (spatial index, threading, reuse) on a representative coverage/route scene; note scaling with core count and receiver density.
- [ ] 5.2 Update README (simulator full-run acceleration; `threadCount` knob) and `openspec/project.md` (move CPU-side full-run acceleration from known gaps to done); run `openspec validate --strict` and archive the change.
