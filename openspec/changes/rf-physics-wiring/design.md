## Context

`rf-physics-advanced` shipped `utdWedgeCoefficient`/`utdTransition` and `reflectDepolarize`/
`polarizationMismatchDb` as analytically-validated primitives, but the UTD path model and the
invocation of depolarization in the reflection builder were deferred pending correct
normalization/gating. This change finishes them.

## Decisions

- **UTD path model = validated half-plane loss.** `utdDiffractionLossDb(v)` evaluates the UTD
  wedge coefficient for a conducting half-plane (n=2) at a reference geometry that maps the
  Fresnel-Kirchhoff parameter v to the diffraction angle, normalized to free space (|D|/sqrt(L)).
  A knife edge IS a conducting half-plane, so this tracks `knifeEdgeLossDb(v)` (v=0 -> ~6 dB;
  within <1 dB across v) — validated by a regression test comparing the two.
- **Depolarization is opt-in.** A V-polarized wave stays V after specular reflection, but a wave
  with both transverse components (e.g. circular) becomes elliptical when TE != TM. Because this
  legitimately changes reflected-path power, it is gated by `enableDepolarization` (default off)
  so archived/golden results are bit-for-bit unchanged; amplitude loss stays in the reflection
  loss and the mismatch normalizes magnitude, so it is not double-counted.

## Risks / Trade-offs

- [UTD half-plane is not a general wedge path model] -> documented: it reuses the dominant-edge v
  as a half-plane; multi-edge/general-wedge UTD remains future work.
- [Depolarization uses a simplified V<->TM, H<->TE per-bounce mapping without incidence-plane
  rotation] -> documented; sufficient to expose the effect, gated off by default.

## Open Questions

None.
