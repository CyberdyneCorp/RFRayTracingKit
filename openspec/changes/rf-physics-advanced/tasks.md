> Deepens RF physics at the CPU core. Every effect is additive and default-neutral — with
> defaults, results stay bit-for-bit identical to the archived behavior. Each formula is
> validated against an ANALYTIC reference value, not just a trend. The default GoogleTest
> suite (160 tests) MUST stay green alongside the new tests.

## 1. Multi-edge diffraction (`multi-edge-diffraction`)

- [x] 1.1 `rf/diffraction_multi.hpp`: a profile type = ordered `(distance, top-height)` samples;
      reuse `fresnelDiffractionParameter` + `knifeEdgeLossDb` from `diffraction.hpp`
- [x] 1.2 Bullington: equivalent single edge from the steepest tx-side and rx-side slopes →
      knife-edge loss at the Bullington point
- [x] 1.3 Deygout: recursive dominant-edge (main + one tx-side + one rx-side sub-recursion) with
      the standard clearance correction
- [x] 1.4 Integrate into the diffraction path finder: a shadowed link builds a profile and uses
      the selected multi-edge loss
- [x] 1.5 Tests: single-obstacle profile reduces EXACTLY to `knifeEdgeLossDb` (analytic);
      two-equal-edge Bullington/Deygout vs a hand-computed reference; loss grows with a second
      obstacle; profile with all samples below LOS → ~0 dB

## 2. UTD wedge diffraction (`utd-diffraction`)

- [x] 2.1 `rf/utd.hpp`: complex Fresnel transition `utdTransition(complex x) -> complex` via the
      closed form F(x)=√(πx)·e^{jπ/4}·w(√x·e^{j3π/4}) (Faddeeva: Taylor for small |z|, Gautschi
      continued fraction for large |z|)
- [x] 2.2 Wedge coefficient `D(phi, phiPrime, beta0, wedgeN, k)` (four cotangent×transition terms
      + `−e^{-jπ/4}/(2n√(2πk)sinβ0)` prefactor; Soft/Hard boundary; L distance parameter)
- [ ] 2.3 (DEFERRED) Wire UTD in as a selectable diffraction PATH model (`DiffractionModel::UTD`) — needs per-edge wedge-geometry extraction + spreading normalization; the UTD coefficient primitive is delivered + validated. Was: selectable via `SimulationSettings`; knife-edge
      stays the default
- [x] 2.4 Tests: `F(x)→1` as `x→∞` (analytic); `F(x)` vs the large-arg asymptotic series and a
      direct numeric-integral evaluation; small-x leading form `√(πx)e^{j(π/4+x)}`; half-plane
      (n=2) GO+diffracted continuous across the incident shadow boundary and −6 dB half-field there
      (matches `knifeEdgeLossDb(0)`)

## 3. Polarization (`polarization`)

- [x] 3.1 `rf/polarization.hpp`: Jones vector (2 complex components); canonical V/H/RHCP/LHCP states
- [x] 3.2 `polarizationMismatchDb(txPol, rxPol)`: co-polar 0 dB, 45° 3.01 dB, orthogonal → large
      (clamped sentinel)
- [x] 3.3 Depolarizing reflection: decompose into TE/TM, apply the respective Fresnel
      coefficients, recompose
- [x] 3.4 Track per-path polarization on `RFPath`; add mismatch to the per-path budget via the
      budget hook, DEFAULT co-polar (0 dB)
- [x] 3.5 Tests: co-polar 0 dB, 45° 3.01 dB, orthogonal large (analytic); RHCP↔LHCP orthogonal;
      reflection off a known material rotates polarization as the Fresnel coefficients predict;
      default co-polar leaves the budget unchanged

## 4. Doppler (`doppler`)

- [x] 4.1 `rf/doppler.hpp`: `perPathDopplerHz(receiverVelocity, arrivalDirUnit, freq) =
      (v·khat)/c·f`, closing ⇒ positive
- [x] 4.2 Add `dopplerHz` field (default 0) to `RFPath` and `RouteSample`
- [x] 4.3 Route simulator derives receiver velocity from consecutive samples (spacing / optional
      speed) and fills per-path Doppler
- [x] 4.4 Tests: closing at v along LOS → `+v/c·f` (analytic); transverse velocity → 0; receding
      → negative; static receiver → 0 exactly

## 5. Multipath coverage (`coverage-multipath`)

- [x] 5.1 Coverage mode via `rayLaunch`: each cell a capture point (radius ≈ cellSize/2) at its
      terrain height; launch rays once per tx; accumulate captured multipath power incoherently
- [x] 5.2 Select ray-launch coverage via `SimulationSettings.mode == RayLaunch`; keep LOS+image
      as the default
- [x] 5.3 Fully-shadowed cells reuse the per-cell knife-edge / multi-edge diffraction fill
- [x] 5.4 Tests: a cell with a strong reflected path shows more power than LOS+FSPL alone;
      default mode (ImageMethod) reproduces the archived per-cell coverage bit-for-bit; no-signal
      cells keep the sentinel

## 6. Budget & result integration (`rf-propagation`, `ray-simulation`)

- [x] 6.1 Wire polarization mismatch into `finishPath` / `extraPropagationLossDb` (0 dB default)
- [x] 6.2 Surface `dopplerHz` on per-path and route results
- [x] 6.3 `SimulationSettings`: diffraction-model selector (knife-edge default; multi-edge / UTD)
      and multipath-coverage selection via `mode`
- [x] 6.4 Documentation: note knife-edge + co-polar + zero-Doppler + image-method-coverage
      defaults and the default-neutral guarantee

## 7. Default-neutral regression & suite

- [x] 7.1 Regression test: a representative scene produces identical paths/coverage with ALL new
      features at their defaults (bit-for-bit vs archived behavior)
- [x] 7.2 Build and run: full suite green (160 existing + new tests) via the documented
      cmake/ctest commands
- [x] 7.3 `openspec validate rf-physics-advanced --strict` passes
