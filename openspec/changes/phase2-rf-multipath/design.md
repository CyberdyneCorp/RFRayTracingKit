## Context

Phase 1 delivered an exact but non-scalable propagation core (image method) plus JSON/CSV
export. Phase 2 makes propagation scale to rich urban multipath and area coverage, and
opens the results up to web/GIS tooling. The image method stays as the correctness oracle:
the new stochastic engine is validated against it on the archived golden scenes. All work
remains on the CPU backend behind the existing `Backend`/`Simulator` boundaries — no GPU,
no Python, no new required dependency.

## Goals / Non-Goals

**Goals:**
- A stochastic ray-launch engine (uniform sphere sampling, multi-bounce specular tracing,
  receiver capture sphere, path dedup) that agrees with the image method within ~1 dB.
- Coverage-grid mode producing a georeferenced 2D power array.
- GeoJSON export (receivers, paths, coverage) and glTF debug export (colored path lines,
  receiver points).
- First-class, tested multi-bounce (`maxReflections` > 1) in both modes.
- Deterministic, seed-driven reproducibility for the stochastic engine.

**Non-Goals:**
- GPU backends, Python bindings, terrain/GeoTIFF, diffraction, atmospheric/vegetation
  attenuation, MIMO/beamforming, route/moving-receiver simulation, CZML/3D-Tiles.
- Coherent field accuracy beyond the existing channel model (no full wave/GO-UTD).

## Decisions

### D1. Two coexisting propagation modes, not a replacement
`SimulationSettings.mode` selects image-method (exact, small scenes, oracle) or ray-launch
(scalable, urban multipath, coverage). The image method is retained precisely so the
stochastic engine has a reference to be validated against. **Alternative:** replace the
image method — rejected; we would lose the oracle and Phase 1's exactness guarantees.

### D2. Uniform sphere sampling via a deterministic low-discrepancy sequence
Rays are generated with a Fibonacci-sphere (or similar) sequence rather than pseudo-random
directions, giving even solid-angle coverage and exact reproducibility without RNG-order
fragility. A seed still parameterizes any jitter. **Alternative:** pure PRNG sampling —
noisier for a given ray budget and harder to reproduce across platforms.

### D3. Capture sphere + path dedup by bounce signature
A ray is captured when a segment passes within `captureRadius` of a receiver; captured
paths are keyed by their ordered reflecting-triangle signature and merged so N rays along
one physical path count once. **Alternative:** no dedup — inflates aggregate power with ray
budget, breaking the image-method agreement test.

### D4. Coverage grid reuses the receiver pipeline
Each grid cell centre is treated as a receiver at the grid height; the coverage array is
assembled from per-cell aggregates. This reuses aggregation/export and keeps one code path.
**Alternative:** a bespoke coverage kernel — premature before GPU work.

### D5. Lightweight, dependency-free exporters
GeoJSON is emitted with nlohmann/json (already a dependency). glTF is written with a small
in-tree glTF 2.0 writer (line/point primitives, embedded buffer) and validated by
re-importing through Assimp (already a dependency). **Alternative:** pull in a glTF library
— unnecessary for line/point debug output.

## Risks / Trade-offs

- **Stochastic vs exact mismatch** → validate ray-launch against the image method on golden
  scenes with a documented ray budget and ≤1 dB tolerance; surface the ray count needed.
- **Ray-budget cost** on large scenes → the capture sphere and dedup bound work; document
  rays/sec and let callers trade rays for accuracy; GPU acceleration is a later phase.
- **Coverage-grid blow-up** (W×H cells × rays) → document cost; keep golden coverage grids
  small; the reuse of the receiver pipeline makes GPU offload straightforward later.
- **glTF writer correctness** → the re-import-through-Assimp test is the acceptance gate.
- **Cross-platform determinism** of the stochastic engine → fixed sampling sequence + seed;
  add a same-seed reproducibility test.

## Migration Plan

Additive over the archived Phase 1 baseline. `SimulationSettings` gains `mode`, `seed`, and
coverage-grid fields with defaults preserving Phase 1 behavior (image method, point
receivers). New exporters are independent entry points. No breaking changes to Phase 1 APIs.

## Open Questions

- glTF debug export: in-tree writer vs a small vendored library — lean in-tree for
  line/point primitives; revisit if mesh export is needed.
- Coverage CSV layout: dense grid matrix vs long `row,col,x,y,power` table — pick one and
  document (leaning long table for GIS friendliness).
- Default ray budget and capture radius for the image-method-agreement tolerance — fix from
  measurement during implementation.
