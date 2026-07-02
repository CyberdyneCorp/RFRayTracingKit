## Context

The CPU engine (Phases 1–7 + core geospatial IO) is the correctness oracle for all GPU
backends. RF formulas live as header-only inline modules under `include/rftrace/rf/` (one
concern per header): `diffraction.hpp`, `fresnel.hpp`, `reflection.hpp`, `phase.hpp`,
`free_space_path_loss.hpp`, `channel.hpp`, `antenna_pattern.hpp`. The single per-path budget
choke point is `detail/propagation.hpp::finishPath(RFPath&, tx, rx, reflectionLossDb, ctx*)`,
and `extraPropagationLossDb(ctx, points)` is where Phase 7 additive loss terms sum.

This change deepens the *physics* at that core. All work is CPU-side, backend-agnostic, and
Python-free. The overriding constraint is **default-neutral**: with default settings and
default inputs the results are bit-for-bit identical to the archived behavior. A regression
test locks this in.

## Goals / Non-Goals

**Goals:**
- Multi-edge diffraction (Bullington + Deygout) over a terrain/building profile, reducing to
  the existing single knife-edge for one obstacle.
- Kouyoumjian–Pathak UTD wedge diffraction (complex transition function + wedge coefficient)
  as a selectable alternative to knife-edge.
- Jones-vector polarization: mismatch loss (co-polar 0 dB, 45° 3.01 dB, orthogonal large) and
  depolarizing reflection via TE/TM Fresnel coefficients.
- Per-path Doppler from receiver velocity and arrival direction; route simulator fills it.
- Multipath coverage via the ray-launch engine (reflections + optional diffraction per cell).
- Each formula checked against an ANALYTIC reference value (not just a trend) with tests, and
  a default-neutral regression test.

**Non-Goals:**
- GPU implementations of these models (later cross-backend task).
- Ray-launched diffraction cones / UTD ray tubes, wideband or time-varying Doppler spectra,
  full 3-D depolarization tensors, and standards-grade calibration.

## Decisions

### D1. Multi-edge diffraction: Bullington AND Deygout on a profile
Both methods operate on a **profile** = ordered `(distance_from_tx, top_height)` samples
between tx and rx (terrain + building tops). Endpoints are the tx and rx antenna heights.

- **Bullington**: from the tx endpoint find the sample maximizing the slope
  `(h_i − h_tx)/d_i`; from the rx endpoint find the sample maximizing the slope toward rx. The
  two lines intersect at a synthetic "Bullington point" whose height/geometry defines an
  equivalent single knife-edge; apply `fresnelDiffractionParameter` + `knifeEdgeLossDb` there.
- **Deygout**: find the obstacle with the largest Fresnel parameter `v` (dominant edge) over
  the full tx→rx span; its knife-edge loss is the main term. Recurse ONCE into the tx-side
  sub-span (tx → dominant edge) and ONCE into the rx-side sub-span (dominant edge → rx), each
  contributing its own dominant-edge knife-edge loss. Sum main + both sub-losses and apply the
  standard Deygout clearance correction so the result stays physically bounded.

Both take a profile and return total diffraction loss in dB. **Reduction guarantee**: a
profile with a single obstacle (one interior sample, or all-but-one samples below the LOS)
yields exactly the value of `knifeEdgeLossDb(fresnelDiffractionParameter(h,d1,d2,lambda))` for
that obstacle — the existing single-edge result. The diffraction path finder builds the
profile between a shadowed tx/rx and uses the selected multi-edge loss.

Header: `include/rftrace/rf/diffraction_multi.hpp`, building on `diffraction.hpp` (it reuses
`fresnelDiffractionParameter` and `knifeEdgeLossDb`; it does not duplicate them).

### D2. UTD via Kouyoumjian–Pathak, knife-edge stays default
Implement the complex Fresnel transition function `F(x)` (the standard `F(x) = 2 j √x e^{jx}
∫_{√x}^∞ e^{−jτ²} dτ` form, with the small-/large-argument asymptotics) as
`utdTransition(std::complex<double> x) -> std::complex<double>`, and the wedge diffraction
coefficient `D(phi, phiPrime, beta0, wedgeN, k)` combining the four cotangent terms times
their transition functions, with the `1/(2n√(2πk) sin β0)` prefactor. `wedgeN` is the exterior
wedge factor (n = exterior wedge angle / π; n=2 is a half-plane/knife-edge). This is offered as
an ALTERNATIVE diffraction model selected via `SimulationSettings` (a diffraction-model
enum/flag); the default remains the knife-edge model, so default results are unchanged.
Analytic checks: `F(x)→1` as `x→∞` (deep lit/shadow), `|F(x)|` and phase at a small reference
`x`, and the half-plane wedge (n=2) recovering the knife-edge shadow-boundary behavior.

Header: `include/rftrace/rf/utd.hpp`.

### D3. Polarization as a Jones vector; depolarizing reflection
A path's polarization is a Jones vector — two `std::complex<double>` components in a transverse
(ê1, ê2) basis. Canonical states: Vertical/Horizontal are the two real basis unit vectors;
RHCP/LHCP are `(1, ∓j)/√2`. `polarizationMismatchDb(txPol, rxPol)` returns
`−10·log10(|⟨rxPol|txPol⟩|² / (‖txPol‖²‖rxPol‖²))`: co-polar → 0 dB, 45° linear offset →
3.01 dB, orthogonal → large (clamped to a documented sentinel). Reflection DEPOLARIZES: the
incident Jones vector is decomposed into TE (perpendicular) and TM (parallel) components
relative to the plane of incidence, each multiplied by the corresponding complex Fresnel
coefficient from `fresnel.hpp`, then recomposed — so a reflection can rotate/ellipticize the
polarization. The mismatch term enters the per-path budget through the existing budget hook
(`extraPropagationLossDb` / `finishPath`), DEFAULT co-polar so it contributes 0 dB and archived
results are unchanged. `RFPath` gains a polarization field (default co-polar with the tx).

Header: `include/rftrace/rf/polarization.hpp`.

### D4. Doppler from receiver velocity and arrival direction
`perPathDopplerHz(receiverVelocity, arrivalDirUnit, freq) = (v · khat)/c · f`, where `khat` is
the unit vector from rx toward the last hop (the path's arrival direction), and the sign
convention is closing ⇒ positive shift. `RFPath` and `RouteSample` gain a `dopplerHz` field
defaulting to 0. The route simulator derives receiver velocity from consecutive samples
(direction from the sample-to-sample displacement, magnitude from spacing / an optional speed)
and fills per-path Doppler; a static receiver (zero velocity) yields 0 exactly. Analytic check:
a receiver closing at v m/s straight along a LOS arrival at frequency f gives `+v/c·f`; a
transverse velocity gives 0; a receding velocity gives the negative.

Header: `include/rftrace/rf/doppler.hpp`.

### D5. Multipath coverage via the ray-launch engine
Add a coverage mode that includes specular reflections (and diffraction when enabled) rather
than LOS + FSPL only. Implementation reuses `rayLaunch`: each grid cell is treated as a capture
point (capture radius ≈ `cellSize/2`) at its terrain height; rays are launched ONCE per
transmitter; captured multipath power is accumulated per cell **incoherently** (power sum).
The existing deterministic per-cell path (LOS + image) stays the DEFAULT; the ray-launch
coverage is selected when `SimulationSettings.mode == RayLaunch`. Fully-shadowed cells may reuse
the per-cell knife-edge / multi-edge diffraction fill. Reduction guarantee: with the default
mode (ImageMethod) the coverage output is unchanged from the archived per-cell result.

## Risks / Trade-offs

- **Deygout over-prediction**: recursive knife-edge summation is known to over-predict; the
  standard clearance correction is applied and documented. Bullington is the conservative
  alternative; both are provided so callers choose.
- **UTD numerics**: the transition function is delicate near shadow boundaries; we use the
  documented asymptotic branches and validate `F(x)→1` plus a reference value. Knife-edge stays
  default so no default path depends on UTD numerics.
- **Ray-launch coverage variance**: stochastic capture introduces sampling noise vs the
  deterministic map; it is opt-in via `mode == RayLaunch` and seeded for reproducibility.
- **Polarization basis bookkeeping**: the transverse basis must be consistent across a path's
  hops; reflection decomposition is done in the local plane-of-incidence and recomposed. Kept
  simple (co-polar default) so the default budget is untouched.

## Migration Plan

Additive only — no breaking changes. New `RFPath`/`RouteSample` fields default to the archived
values (co-polar polarization, `dopplerHz = 0`); new `SimulationSettings` fields default to the
knife-edge diffraction model and the image-method coverage. Implement as focused sub-changes
(diffraction-multi, UTD, polarization, Doppler, multipath-coverage), each with analytic-
reference tests, and a shared default-neutral regression test that asserts a representative
scene produces identical paths/coverage with all new features at their defaults. The default
GoogleTest suite (currently 160 tests) MUST stay green alongside the new tests.

## Open Questions

None — D1–D5 are resolved above and are to be implemented as specified.
