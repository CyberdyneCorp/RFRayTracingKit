> Large phase — see design.md "Migration Plan". Implement as focused sub-changes (one per
> capability group) and archive each independently; this list is the full Phase 7 surface.

## 1. Diffraction (`diffraction`)

- [ ] 1.1 `rf/diffraction.hpp`: Fresnel parameter v and ITU-R P.526 knife-edge loss (v=0 ≈ 6 dB)
- [ ] 1.2 Obstacle silhouette-edge candidate enumeration near a blocked LOS
- [ ] 1.3 Diffracted-path generation behind `enableDiffraction`; path type `diffraction` + count
- [ ] 1.4 Tests: v=0 ≈6 dB, monotonic into shadow, clear-LOS ≈0 dB, shadowed rx gains a path, disabled = none

## 2. Atmospheric & vegetation attenuation (`atmospheric-attenuation`, `vegetation-attenuation`)

- [ ] 2.1 `rf/atmospheric.hpp`: rain γ_R = k·R^α (P.838) + gaseous approx (P.676)
- [ ] 2.2 `rf/vegetation.hpp`: Weissberger/P.833 foliage loss vs depth, bounded
- [ ] 2.3 In-foliage depth from vegetation-tagged geometry along a path
- [ ] 2.4 Wire both into the per-path budget (default off)
- [ ] 2.5 Tests: rain scales with rate / negligible <5 GHz; foliage grows with depth / 0 at 0; disabled = Phase 1/2 budget

## 3. Antenna arrays (`antenna-arrays`)

- [ ] 3.1 `rf/array.hpp`: ULA/UPA element geometry + per-element weights
- [ ] 3.2 Array factor gain + beam steering (weights to point at a direction)
- [ ] 3.3 Feed steered array gain into the link-budget antenna term
- [ ] 3.4 Tests: steered beam peaks at target, roll-off away, array gain in a path budget

## 4. MIMO channel (`mimo-channel`)

- [ ] 4.1 `rf/mimo.hpp`: assemble H (N×M complex) from per-path gains + array responses
- [ ] 4.2 Capacity log2 det(I + (SNR/M)·H·Hᴴ) + per-stream SINR
- [ ] 4.3 Tests: H dims N×M, single-path ≈ rank 1, capacity higher for well-conditioned vs rank-1

## 5. Cell planning / SINR (`cell-planning`)

- [ ] 5.1 `core/cell_planning.*`: per-receiver SINR (serving vs interferers + noise floor)
- [ ] 5.2 Serving-cell selection (best server)
- [ ] 5.3 SINR coverage map (reuse coverage-grid mode)
- [ ] 5.4 Extend results-export with serving id / SINR / interference
- [ ] 5.5 Tests: SINR = S/(I+N); single tx = SNR; strongest serves; SINR coverage array + sentinel

## 6. Route simulation (`route-simulation`)

- [ ] 6.1 `core/route.*`: waypoints → sampled positions (spacing or timestamps)
- [ ] 6.2 Route mode: evaluate each sample as a point receiver → ordered series
- [ ] 6.3 Route JSON/CSV export (drive-test style, sample order)
- [ ] 6.4 Tests: K samples → K ordered results; CSV one row per sample in order

## 7. Settings, integration & validation

- [ ] 7.1 Extend `SimulationSettings` (enableDiffraction, rain rate, vegetation, SINR/noise) — all default off
- [ ] 7.2 Golden: archived Phase 1/2/4 results unchanged with all features off
- [ ] 7.3 Examples/docs for a diffraction scene, an SINR coverage map, and a drive-test route
- [ ] 7.4 `just ci` (and `just metal` where relevant) green; `openspec validate --all --strict` passes
