## 1. Phase 1 — wedge-geometry extraction

- [x] 1.1 Build an edge→incident-faces map over `scene.triangles()`; classify a diffracting edge as free (one face → half-plane, `n = 2`) or a shared non-coplanar edge (real wedge).
- [x] 1.2 Compute the interior/exterior wedge angle from the two faces' dihedral (convex corner → exterior `= 2π − interior`), `n = exterior/π`; determine the o-face and 0/n-face orientation for consistent `phi`/`phiPrime`.
- [x] 1.3 Compute the UTD angles from tx/edge/rx: `phiPrime` (incidence), `phi` (diffraction) about the edge from the o-face, and `beta0` (skew of the incident ray to the edge tangent).
- [x] 1.4 Unit-test extraction on known geometry: a wall's free top edge → `n = 2`; a 90° box corner → `n = 1.5`; degenerate/coplanar shared edges skipped.

## 2. Phase 1 — geometry-driven single-wedge loss

- [x] 2.1 Add `rf::utdWedgePathLossDb(...)` in `utd.hpp` (or a `rf/utd_geometry` unit): loss from `utdWedgeCoefficient(phi, phiPrime, beta0, n, k, L)` with `L = s·s'·sin²β0/(s+s')` and spreading `A(s',s) = sqrt(s'/(s(s'+s)))`, normalized so `n = 2` reproduces the knife-edge curve.
- [x] 2.2 Wire `diffractionLossDb(..., DiffractionModel::UTD)` in `src/simulator.cpp` to the geometry-driven path (extract wedge geometry for the chosen edge, compute the loss); keep knife-edge the default.
- [x] 2.3 Verify finiteness for all valid geometries (grazing, shadow boundary, deep shadow); clamp/guard degenerate cases.

## 3. Phase 1 — physical validation gates

- [x] 3.1 Reduce-to-knife-edge: a half-plane edge (`n = 2`) UTD loss matches ITU-R knife-edge across a v-sweep within the documented tolerance.
- [x] 3.2 Reciprocity: swapping tx↔rx gives equal loss (within tolerance).
- [x] 3.3 Shadow-boundary continuity: sweeping the receiver through the incident shadow boundary yields a continuous total (GO + diffracted) field — no step/spike.
- [x] 3.4 Monotonic shadowing: loss increases monotonically as the receiver moves deeper into shadow.
- [x] 3.5 Wedge-angle sensitivity: a sharper wedge diffracts differently than a half-plane, within physical bounds; determinism across repeated runs.
- [x] 3.6 Confirm the golden/regression suites stay green (knife-edge default unchanged; non-UTD results unaffected).

## 4. Phase 2 — multi-edge UTD cascade

- [ ] 4.1 Add a UTD multi-edge cascade (Deygout-analog: dominant edge → recurse on sub-links), mirroring `diffraction_multi.hpp`'s structure but with UTD per-edge loss.
- [ ] 4.2 Single obstructing edge reduces exactly to the Phase-1 single-wedge result; multiple edges accumulate a finite total ≥ the strongest single edge.
- [ ] 4.3 Tests: single-edge reduction, multi-edge accumulation, reciprocity, determinism; golden suites green.

## 5. Docs & archive

- [ ] 5.1 Update `include/rftrace/rf/utd.hpp` docs and add a short note in the RF section of the README on the geometry-driven UTD path model + its validation properties.
- [ ] 5.2 Update `openspec/project.md` (general multi-edge/wedge UTD moved from not-built to done). Run `openspec validate --strict` and archive the change (syncing the modified `utd-diffraction` requirement + the new `utd-wedge-path` capability).
