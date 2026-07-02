## Context

Phases 1–4 give a validated CPU multipath engine (LOS + specular reflection + ray-launch),
coverage grids, Python bindings, and a Metal traversal backend. Phase 7 adds the physically
richer effects and system-level metrics needed for real cellular planning. Everything here is
CPU-side and validated against analytic or published-model references — the CPU engine remains
the oracle. Each model is additive and default-off, so archived Phase 1/2/4 behavior is
preserved bit-for-bit unless a feature is explicitly enabled.

## Goals / Non-Goals

**Goals:**
- Diffraction (knife-edge / multiple-edge), rain + gaseous + vegetation attenuation, antenna
  arrays + beam steering, MIMO channel + capacity, SINR/serving-cell + SINR coverage, and
  moving-receiver route simulation — each with reference-checked tests.
- Additive integration into the existing per-path budget and result/coverage/export pipeline.

**Non-Goals:**
- GPU implementations of these models (a later cross-backend task).
- Full ray-optical UTD/GTD, ray-launched diffraction cones, terrain/GeoTIFF ingestion,
  CZML/3D-Tiles route animation, standards-grade calibration/certification.

## Decisions

### D1. Published models with documented references, not bespoke physics
Use ITU-R P.526 (diffraction), P.838 (rain), P.676 (gaseous), P.833/Weissberger (vegetation),
and standard array-factor / log-det MIMO capacity. Each formula module cites its reference and
is unit-tested against a known value. Rationale: reviewable, defensible, comparable to other
planning tools.

### D2. Additive, default-off features
New losses/metrics are gated by `SimulationSettings` flags defaulting off; the per-path budget
sums extra terms that are 0 when disabled. This guarantees the archived golden results are
unchanged and makes each feature independently testable.

### D3. Diffraction via geometric edge candidates + knife-edge loss (not UTD)
Phase 7 finds diffraction over obstacle silhouette edges (top-edge / vertical-edge candidates)
and applies knife-edge loss from the Fresnel parameter, extending to Bullington/Deygout-style
multiple edges. Full UTD wedge diffraction is a non-goal. Rationale: matches the source spec's
"basic diffraction approximation" and is analytically checkable.

### D4. Arrays/MIMO on Eigen; complex path gains
Array factor and the MIMO channel matrix are built from per-path complex gains (amplitude +
accumulated phase, already computed) and per-element steering vectors, using Eigen complex
matrices. Capacity uses log2 det(I + (SNR/M)·H·Hᴴ). Rationale: no new dependency; reuses the
existing coherent-path machinery.

### D5. SINR reuses the point-receiver and coverage pipelines
SINR is computed by running the existing per-transmitter aggregation for all transmitters at a
receiver/cell, then combining (serving vs interferers + noise). Coverage SINR reuses
coverage-grid mode. Rationale: one code path, consistent with Phase 2.

### D6. Route mode reuses point-receiver evaluation per sample
A route is sampled into positions; each is evaluated as a point receiver and results are
collected in order. Rationale: deterministic, trivially correct, and drive-test CSV falls out
naturally.

## Risks / Trade-offs

- **Scope size** → this phase is 7 features. See "Migration Plan": implement as sub-changes,
  not one apply, to keep each reviewable and independently validated.
- **Model fidelity vs simplicity** → knife-edge (not UTD) and approximate P.676 are documented
  approximations; tests assert reference values within stated tolerances, not certification.
- **Diffraction edge enumeration cost** on large scenes → bound candidate edges (top edges of
  blocking geometry near the LOS); document limits, defer scalable diffraction to GPU phase.
- **MIMO validation** → validate matrix dimensions/rank and monotonic capacity trends rather
  than absolute values, which depend on scene richness.

## Migration Plan

Additive over the archived Phases 1–4. **Recommended implementation split** into sub-changes
so each ships validated independently (this scaffold captures the full Phase 7 spec surface;
when implementing, create focused changes and archive them one at a time):
1. `diffraction` (+ ray-simulation/rf-propagation hooks)
2. `atmospheric-attenuation` + `vegetation-attenuation` (+ rf-propagation hook)
3. `antenna-arrays`
4. `mimo-channel`
5. `cell-planning` (SINR) (+ results-export hook)
6. `route-simulation` (+ ray-simulation/results-export hooks)
No breaking API changes; all new fields/flags default off.

## Open Questions

- Split this into the six sub-changes above for implementation (recommended), or implement as
  one large change?
- Diffraction edge model: single knife-edge only first, or Deygout/Bullington multi-edge in
  the first cut?
- Vegetation input: tag by material only, or also support explicit vegetation volumes/boxes?
- SINR noise floor: fixed dBm default vs bandwidth/temperature-derived (kTB + noise figure)?
- MIMO capacity assumption: equal-power vs water-filling; single-frequency (narrowband) first?
