## Context

`rf::utdWedgeCoefficient(phi, phiPrime, beta0, wedgeN, k, L, bc)` (in `include/rftrace/rf/utd.hpp`) is
the full, analytically-validated Kouyoumjian–Pathak coefficient (any wedge, soft/hard, Faddeeva
transition). But the simulator's UTD path (`diffractionLossDb` → `rf::utdDiffractionLossDb(v)`) throws
that generality away: it hardcodes `n = 2`, `d1 = d2 = 1000`, `λ = 1`, and maps `v → loss` so the
result only mirrors the ITU-R knife edge. The diffracting edges come from `boundaryEdges` — *open*
silhouette edges (single triangle), which are inherently half-planes; true wedges (shared,
non-coplanar edges, i.e. building corners) are explicitly excluded there.

To make UTD physically meaningful this change adds the **geometry-driven path model**: extract the
real wedge angle + UTD angles from the mesh and link, compute loss with proper spreading, and cascade
across edges. Knife-edge stays the default; a half-plane edge must still reduce to the knife-edge loss.

## Goals / Non-Goals

**Goals:**
- Extract per-edge wedge geometry (`n`, `phi`, `phiPrime`, `beta0`, `L`, `s'`, `s`) from the scene.
- Compute single-wedge diffracted loss from `utdWedgeCoefficient` + spherical spreading, reducing to
  knife-edge for `n = 2`.
- Cascade UTD across multiple edges (Phase 2).
- Prove correctness with physical gates (reciprocity, shadow continuity, monotonicity,
  reduce-to-knife-edge) — no default-result change.

**Non-Goals:**
- Not changing the knife-edge or Bullington/Deygout models, nor the default.
- Not full 3-D creeping-wave / curved-surface UTD, slope diffraction, or double-diffraction
  cross-terms in v1 — cascade is the successive-edge approximation (Deygout analog), documented.
- Not exposing new required settings; `DiffractionModel::UTD` gains geometry awareness in place.

## Decisions

### D1 — Phase the work; Phase 1 = single wedge, Phase 2 = multi-edge cascade
Phase 1 delivers geometry-driven single-wedge loss (the hard, foundational, well-validatable core) and
lands behind the existing `DiffractionModel::UTD`. Phase 2 adds the multi-edge cascade. Each phase is
gated by the physical tests and keeps knife-edge the default. **Why:** the single-wedge physics +
geometry extraction is where correctness risk concentrates; validate it before cascading.

### D2 — Wedge-geometry extraction from mesh topology
Build an edge→incident-faces map over `scene.triangles()`. A diffracting edge is either a free edge
(one face → half-plane, `n = 2`) or a shared edge whose two faces are non-coplanar (a real wedge). The
**interior** wedge angle is the dihedral angle between the two faces measured through the *material*
side; the **exterior** (illuminated) wedge angle is `2π − interior` for a convex corner, and
`n = exterior_angle / π`. Determine the wedge o-face (the reference face) and the `0`/`n`-faces so
`phi`/`phiPrime` are measured consistently (per the McNamara convention the coefficient uses). Compute
`beta0` from the angle between the incident ray and the edge tangent. **Why:** these are exactly the
inputs `utdWedgeCoefficient` documents; getting the interior/exterior sign and o-face right is the
main geometry risk — covered by the reduce-to-knife-edge and wedge-angle-sensitivity tests.
*Alternative considered:* infer wedge angle from a heuristic — rejected; use real mesh dihedral.

### D3 — Amplitude: distance parameter L and spherical spreading
For spherical-wave incidence on a straight edge, `L = s·s'·sin²β0 / (s + s')` and the diffracted field
`E_d = E_i · D · A(s',s) · e^{-jks}` with `A(s',s) = sqrt(s' / (s(s'+s)))`. The path loss is the extra
loss over the direct/free-space reference, i.e. `−20·log10|D·A|` normalized so a half-plane reproduces
the knife-edge curve. **Why:** the current code fakes spreading with a fixed-geometry ratio; correct
`L` and `A` make the loss track the true link. *Validation:* the `n = 2` reduction test pins the
normalization to the ITU-R knife edge.

### D4 — Keep `DiffractionModel::UTD`; half-plane is the limiting case
`diffractionLossDb(..., UTD)` calls the new geometry-driven path (single wedge in Phase 1) instead of
`utdDiffractionLossDb(v)`. For a half-plane edge it must reproduce the knife-edge loss (so the existing
`UTD tracks knife-edge` scenario still holds). `utdDiffractionLossDb(v)` may remain as the documented
half-plane reference used by that limiting-case test. **Why:** additive/behavioural upgrade in place,
default-neutral (knife-edge remains default; non-UTD results untouched).

### D5 — Multi-edge cascade (Phase 2): Deygout-analog with UTD coefficients
Find the dominant obstructing edge, apply UTD there, then recurse on the sub-links (tx→edge and
edge→rx) for secondary edges, summing the per-edge diffraction losses (dB). A single obstructing edge
reduces exactly to Phase 1. **Why:** mirrors the validated `deygoutRecurse` structure in
`diffraction_multi.hpp`, but with UTD per-edge loss. *Non-goal:* rigorous double-diffraction
transition cross-terms.

### D6 — Validation is physical, not a golden number
UTD has no simple closed-form scene answer, so gates are properties: (a) reduce-to-knife-edge for
`n = 2` across a v-sweep (tolerance ≈ the existing UTD/knife-edge tolerance); (b) reciprocity under
tx↔rx swap; (c) continuity — sweep the receiver across the shadow boundary and assert the total field
has no discontinuity; (d) monotonic loss deepening into shadow; (e) wedge-angle sensitivity bounds
(sharper wedge diffracts differently, within physical bounds); (f) determinism. Plus the existing
golden/regression suites stay green (knife-edge default unchanged).

## Risks / Trade-offs

- **[UTD angle-convention / interior-vs-exterior / o-face errors]** → pin with the reduce-to-knife-edge
  (`n = 2`) test and wedge-angle-sensitivity bounds; follow the McNamara convention the coefficient
  documents; verify the diff by hand.
- **[Spreading/normalization wrong → loss off by a scale]** → the `n = 2` reduction test fixes the
  normalization against the ITU-R knife edge; reciprocity catches asymmetry.
- **[Shadow-boundary discontinuity]** → the transition function guarantees continuity; the continuity
  sweep test asserts it directly.
- **[Mesh dihedral / degenerate faces]** → clamp/skip degenerate or coplanar shared edges (treat as
  no-wedge); free edges default to half-plane.
- **[Changing default results]** → knife-edge stays default; only `DiffractionModel::UTD` behaviour
  changes, and only its half-plane limit is asserted vs the prior behaviour.

## Migration Plan

- PR 1 (Phase 1): wedge-geometry extraction + geometry-driven single-wedge `utdWedgePathLossDb` +
  wire `diffractionLossDb(UTD)` + the physical-gate tests. Knife-edge default unchanged.
- PR 2 (Phase 2): multi-edge UTD cascade + its tests.
- Update the `utd-diffraction` living spec (the MODIFIED requirement) and the roadmap; archive.

## Open Questions

- Interior-vs-exterior wedge-angle determination for arbitrary meshes (need a consistent
  "material side" orientation) — use face winding/normals; validate on box corners with known angles.
- Whether Phase 1 should also expose the wedge angle for debugging (e.g. a diagnostic) — optional.
- Multi-edge: strict Deygout dominant-edge recursion vs a fixed edge order — start Deygout-analog.
