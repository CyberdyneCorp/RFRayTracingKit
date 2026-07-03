## Why

UTD is a selectable diffraction model, but its diffracted-path loss (`rf::utdDiffractionLossDb(v)`)
collapses the general Kouyoumjian–Pathak wedge coefficient to a **fixed conducting half-plane**
(`n = 2`) with a hardcoded reference geometry (d1 = d2 = 1000 m, λ = 1), mapping the Fresnel parameter
`v` to a loss that merely tracks the ITU-R knife edge. The general wedge coefficient
(`utdWedgeCoefficient`, any wedge angle, soft/hard, Faddeeva transition) already exists and is
analytically validated — but the simulator never feeds it real scene geometry. The documented
follow-up (per the `utd-diffraction` spec note) is a **geometry-driven UTD path model**: per-edge
wedge-angle extraction, real incidence/diffraction/skew angles, correct spherical spreading, and
multi-edge cascading. That is what makes UTD physically meaningful for real building corners rather
than a knife-edge look-alike.

## What Changes

- **Wedge-geometry extraction**: from the scene mesh, identify diffracting edges and their true wedge
  angle `n` (from the two faces meeting at the edge; a shared, non-coplanar edge is a real wedge — a
  90° building corner gives an exterior angle 270° → `n = 1.5`; a free/single-face edge remains a
  half-plane `n = 2`). Compute the UTD angles (`phiPrime` incidence, `phi` diffraction, `beta0` skew)
  from tx/edge/rx relative to the wedge o-face.
- **Geometry-driven single-wedge path loss (Phase 1)**: replace the fixed-geometry `v`-mapping with a
  loss computed from `utdWedgeCoefficient` using the extracted `n` and angles and the correct UTD
  distance parameter `L` and spherical spreading factor `A(s',s) = sqrt(s'/(s(s'+s)))`, so the loss
  reflects the actual link and wedge.
- **Multi-edge UTD cascade (Phase 2)**: chain UTD diffraction across several obstructing edges (a UTD
  analog of the Deygout dominant-edge recursion), each edge's diffracted field feeding the next.
- **Default-neutral & backward-compatible**: the knife-edge model stays the default; a half-plane edge
  (`n = 2`) SHALL reduce to the existing knife-edge-tracking loss (a limiting case), so the
  `UTD tracks knife-edge` behaviour is preserved. Archived non-UTD results are unchanged.

## Capabilities

### New Capabilities
- `utd-wedge-path`: a geometry-driven UTD diffraction path model — per-edge wedge-angle extraction,
  real UTD angles, spherical spreading, and multi-edge cascading — that reduces to the knife-edge in
  the half-plane limit and is validated by reciprocity, shadow-boundary continuity, monotonic
  shadowing, and reduction-to-knife-edge tests.

### Modified Capabilities
- `utd-diffraction`: the `UTD as a selectable diffraction path model` requirement changes from
  "computed from the conducting-half-plane coefficient" to "computed from the wedge coefficient using
  the edge's extracted wedge geometry, reducing to the half-plane/knife-edge loss for a half-plane
  edge". (The transition-function and wedge-coefficient primitive requirements are unchanged.)

## Impact

- **Code**: `include/rftrace/rf/utd.hpp` (a geometry-driven `utdWedgePathLossDb(...)` taking real
  angles/wedge/L, plus a spreading factor); a new wedge-extraction helper (mesh topology → shared
  non-coplanar edges + wedge angle + o-face) likely in `src/simulator.cpp` or a new
  `rf/utd_geometry` unit; `diffractionLossDb`/`diffractionPath`/`boundaryEdges` in `src/simulator.cpp`
  to supply wedge geometry and (Phase 2) cascade; a UTD multi-edge module mirroring
  `diffraction_multi.hpp`.
- **Public API**: additive/behavioural only — `DiffractionModel::UTD` gains geometry awareness; no
  new required settings; the knife-edge default and archived non-UTD results are unchanged.
- **Tests**: analytic/physical gates — reduces to knife-edge for a half-plane edge; reciprocity
  (tx↔rx); continuity across the shadow boundary (no discontinuity as the receiver crosses it);
  monotonic loss deepening into shadow; wedge-angle sensitivity bounds; multi-edge reduces to
  single-edge when one edge dominates; determinism.
- **Risk**: physics correctness (UTD angle conventions, spreading, wedge-angle sign/interior-exterior,
  multi-edge cascade) and mesh-topology wedge detection — contained by the analytic gates above and
  by keeping knife-edge the default so no default result changes.
