## Why

The `rf-physics-advanced` change delivered the UTD wedge coefficient and Jones-vector
depolarization as validated primitives but explicitly deferred wiring them into the
simulation path. This change completes those two follow-ups.

## What Changes

- Add `DiffractionModel::UTD`: diffracted paths use a UTD conducting-half-plane loss
  (`rf::utdDiffractionLossDb(v)`) derived from the UTD wedge coefficient. Knife-edge stays
  the default; UTD tracks the ITU-R knife-edge loss (a knife edge is a half-plane) within a
  fraction of a dB.
- Invoke reflection depolarization in the reflection path builder, gated by a new
  `SimulationSettings.enableDepolarization` (default off): each bounce applies its complex
  TE/TM Fresnel coefficients to the path's Jones vector, so unequal TE/TM makes the reflected
  wave elliptical and the polarization-mismatch term grows. Default off keeps co-polar/archived
  results unchanged.

## Capabilities

### Modified Capabilities
- `utd-diffraction`: UTD is now a selectable diffraction path model (was a deferred follow-up).
- `polarization`: reflection depolarization is now applied on the reflected path (opt-in).

## Impact

- `include/rftrace/rf/utd.hpp` (adds `utdDiffractionLossDb`), `include/rftrace/simulator.hpp`
  (`DiffractionModel::UTD`, `enableDepolarization`), `src/simulator.cpp` (wiring),
  `include/rftrace/detail/propagation.hpp` (`finishPath` accepts a depolarized Jones).
  Additive/default-neutral; no dependency changes.
