## 1. UTD path model

- [x] 1.1 `rf::utdDiffractionLossDb(v)` from the UTD half-plane coefficient (tracks knife-edge)
- [x] 1.2 `DiffractionModel::UTD` selectable; wired into the diffraction path finder
- [x] 1.3 Tests: UTD loss vs knife-edge across v; UTD selectable produces a finite diffraction path

## 2. Reflection depolarization

- [x] 2.1 `SimulationSettings.enableDepolarization` (default off); `finishPath` accepts a depolarized Jones
- [x] 2.2 Reflection builder accumulates the Jones via per-bounce TE/TM Fresnel coefficients
- [x] 2.3 Tests: opt-in + lossy for circular pol; default-neutral (co-polar unchanged)

## 3. Validation

- [x] 3.1 Default build green (220/220); `openspec validate --all --strict` passes
