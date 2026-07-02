## Why

Phase 7 gave the CPU engine single knife-edge diffraction, atmospheric/vegetation loss,
arrays, MIMO, SINR and route simulation. Real 4G/5G/6G planning needs more physical fidelity
at the propagation core: links are usually shadowed by *several* obstacles (not one), building
corners diffract as *wedges* rather than knife-edges, antennas and reflections change the wave
*polarization* (co/cross-polar mismatch is a first-order link-budget term), moving receivers
see a *Doppler* shift that drive-test and 5G-NR analysis depend on, and a useful coverage map
must include *multipath* (reflections/diffraction), not just LOS + FSPL.

This change deepens the RF physics at the C++ core only. Each effect is a header-only inline
module under `include/rftrace/rf/` (one concern per header, matching the existing style),
backend-agnostic and Python-free. Every effect is **additive and default-neutral**: with
default settings and inputs, results stay bit-for-bit identical to the archived behavior
(co-polar polarization → 0 dB mismatch; Doppler → 0 for static receivers; single-edge
diffraction reduces exactly to `knifeEdgeLossDb`; default coverage stays LOS + image method).
A regression test confirms the default-neutral guarantee.

## What Changes

- **Multi-edge diffraction**: `rf/diffraction_multi.hpp` implementing BOTH the Bullington
  equivalent-single-edge construction (from the steepest tx-side and rx-side slopes) and the
  Deygout recursive dominant-edge method (one sub-recursion each side, standard clearance
  correction), operating on a terrain+building PROFILE = ordered (distance, top-height)
  samples between tx and rx. A single-obstacle profile reduces exactly to `knifeEdgeLossDb`.
  The diffraction path finder uses the multi-edge loss for shadowed links.
- **UTD diffraction**: `rf/utd.hpp` implementing the Kouyoumjian–Pathak UTD wedge diffraction
  coefficient with the complex Fresnel transition function `F(x)`: `utdTransition(x)->complex`
  and a wedge coefficient `D(phi, phiPrime, beta0, wedgeN, k)`. Offered as an *alternative*
  diffraction model selectable via settings; knife-edge stays the default.
- **Polarization**: represent a path's polarization as a Jones vector (two complex components
  in a transverse basis). `polarizationMismatchDb(txPol, rxPol)` (co-polar 0 dB, orthogonal →
  large, 45° → 3.01 dB); reflection DEPOLARIZES by applying the TE and TM Fresnel coefficients
  to the respective Jones components. The mismatch term enters the per-path budget via the
  existing budget hook, DEFAULT co-polar (0 dB). Per-path polarization tracked on `RFPath`.
- **Doppler**: `perPathDopplerHz(receiverVelocity, arrivalDirUnit, freq) = (v·khat)/c·f` with
  `khat` the unit vector from rx toward the last hop (arrival direction; closing ⇒ positive).
  New `dopplerHz` field on `RFPath` and `RouteSample` (default 0). The route simulator derives
  receiver velocity from consecutive samples and fills per-path Doppler; static receivers ⇒ 0.
- **Multipath coverage**: a coverage mode that includes specular reflections (and diffraction
  when enabled) instead of LOS + FSPL only, via the ray-launch engine — each grid cell is a
  capture point (radius ≈ cellSize/2) at its terrain height, rays are launched ONCE per
  transmitter, and captured multipath power is accumulated per cell (incoherent). The existing
  deterministic per-cell (LOS + image) path stays the default; ray-launch coverage is selected
  when `SimulationSettings.mode == RayLaunch`.
- Wire the new terms into the per-path budget (`rf-propagation`: polarization mismatch +
  Doppler) and the diffraction/coverage behavior into `ray-simulation` (multi-edge / UTD model
  selection; multipath coverage mode).

## Capabilities

### New Capabilities
- `multi-edge-diffraction`: profile-based Bullington + Deygout multi-obstacle diffraction loss,
  reducing to single knife-edge for one obstacle.
- `utd-diffraction`: Kouyoumjian–Pathak UTD wedge coefficient and complex Fresnel transition
  function, as an alternative diffraction model.
- `polarization`: Jones-vector path polarization, co/cross-polar mismatch loss, and
  depolarizing reflection via TE/TM Fresnel coefficients.
- `doppler`: per-path Doppler shift from receiver velocity and arrival direction.
- `coverage-multipath`: ray-launch coverage that accumulates multipath (reflection/diffraction)
  power per cell.

### Modified Capabilities
- `rf-propagation`: add the polarization-mismatch and Doppler terms to per-path results
  (mismatch in the budget via the budget hook; Doppler as a per-path frequency-shift field).
- `ray-simulation`: add multi-edge / UTD diffraction model selection (knife-edge default) and
  the multipath coverage mode (LOS + image default).

## Impact

- **Code (new):** `include/rftrace/rf/diffraction_multi.hpp`, `include/rftrace/rf/utd.hpp`,
  `include/rftrace/rf/polarization.hpp`, `include/rftrace/rf/doppler.hpp`; extends
  `RFPath`/`RouteSample` (polarization + `dopplerHz`), `SimulationSettings` (diffraction-model
  selector), `detail/propagation.hpp` budget hook, `src/simulator.cpp` diffraction path finder
  and coverage, and reuses `src/backends/cpu_nanort/raylaunch.cpp` for multipath coverage.
- **Dependencies:** none new (Eigen/`std::complex` cover Jones vectors and the transition
  function).
- **Default-neutral:** every new field/flag defaults to the archived behavior; a regression
  test asserts identical results with defaults.
- **Out of scope:** GPU implementations of these models, ray-launched diffraction cones,
  wideband/time-varying Doppler spectra, full 3-D depolarization tensors, and calibration.
