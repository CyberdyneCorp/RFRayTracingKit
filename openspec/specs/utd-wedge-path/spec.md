# utd-wedge-path Specification

## Purpose
A geometry-driven UTD diffraction path model: per-edge wedge-angle extraction from the mesh dihedral, real UTD angles + spherical spreading for the dominant edge, and an additive UTD Deygout cascade over the terrain profile for doubly-obstructed links. Reduces to the ITU-R knife edge in the half-plane limit; validated by reciprocity, shadow-boundary continuity, and monotonic shadowing. Knife-edge remains the default.
## Requirements
### Requirement: Per-edge wedge-geometry extraction
The UTD path model SHALL derive, for a diffracting edge, the wedge parameters the coefficient needs:
the exterior wedge factor `n` from the two mesh faces meeting at the edge (a shared, non-coplanar edge
is a real wedge; a free/single-face edge is a half-plane with `n = 2`), the incidence angle
`phiPrime` and diffraction angle `phi` measured from the wedge o-face about the edge, and the skew
angle `beta0` of the incident ray to the edge — all computed from the transmitter, receiver, and edge
geometry.

#### Scenario: Half-plane edge yields n = 2
- **WHEN** the diffracting edge belongs to a single face (a free silhouette edge)
- **THEN** the extracted wedge factor SHALL be `n = 2` (a conducting half-plane)

#### Scenario: Right-angle building corner yields the wedge factor
- **WHEN** the diffracting edge is shared by two faces meeting at a 90° interior angle (exterior wedge
  angle 270°)
- **THEN** the extracted wedge factor SHALL be `n = 1.5` (exterior angle / π)

### Requirement: Geometry-driven single-wedge diffracted loss
For a link blocked by a single diffracting edge with the UTD model selected, the simulator SHALL
compute the diffracted-path loss from the Kouyoumjian–Pathak wedge coefficient evaluated with the
extracted wedge factor and angles, the UTD distance parameter `L`, and the spherical-wave spreading
factor `A(s', s) = sqrt(s' / (s·(s' + s)))`, where `s'` and `s` are the incident and diffracted ray
lengths. The result SHALL be finite for all valid geometries.

#### Scenario: Loss reflects the real link and wedge
- **WHEN** a blocked link is diffracted over an edge whose wedge factor and angles are extracted from
  the scene
- **THEN** the loss SHALL be computed from `utdWedgeCoefficient` with those parameters and the
  spreading factor, and SHALL be finite (no NaN/inf)

#### Scenario: Reduces to knife-edge for a half-plane edge
- **WHEN** the diffracting edge is a half-plane (`n = 2`)
- **THEN** the geometry-driven UTD loss SHALL match the ITU-R knife-edge loss within the documented
  tolerance across the Fresnel parameter range (≈6 dB at the shadow boundary), preserving the prior
  UTD-tracks-knife-edge behaviour

### Requirement: Physical consistency of the UTD path loss
The geometry-driven UTD diffracted loss SHALL satisfy the physical properties of a valid diffraction
model: reciprocity, continuity across the shadow boundary, and monotonic deepening into shadow.

#### Scenario: Reciprocity
- **WHEN** the transmitter and receiver positions are swapped for the same scene and edge
- **THEN** the diffracted-path loss SHALL be equal (within numerical tolerance)

#### Scenario: Continuity across the shadow boundary
- **WHEN** the receiver is swept through the incident shadow boundary of the edge
- **THEN** the total (geometric-optics + diffracted) field magnitude SHALL be continuous — no step or
  spike at the boundary — as guaranteed by the UTD transition function

#### Scenario: Monotonic shadowing
- **WHEN** the receiver moves progressively deeper into the geometric shadow behind the edge
- **THEN** the diffracted-path loss SHALL increase monotonically

### Requirement: Multi-edge UTD for doubly-obstructed links
The UTD path model SHALL produce a finite diffracted-path loss for a **doubly-obstructed** link — one
where no single edge provides a clearing detour, the case the single-wedge selection would otherwise
reject — by a successive-edge (Deygout) construction over the terrain profile using UTD per-edge loss.
This SHALL be **additive**: when a single edge provides a clearing detour the model SHALL use the
single-wedge path unchanged (so its results are identical to the single-wedge requirement); the
multi-edge construction SHALL only apply where the single-wedge selection finds no clearing edge, so
no previously-produced result changes. The multi-edge loss SHALL be reciprocal and deterministic.

#### Scenario: Single obstruction uses the single-wedge path
- **WHEN** exactly one edge obstructs the link (a clearing detour exists)
- **THEN** the loss SHALL equal the single-wedge UTD loss for that edge (the multi-edge construction
  SHALL NOT alter it)

#### Scenario: Doubly-obstructed link yields a finite cascaded loss
- **WHEN** the link is obstructed by multiple obstacles in series such that no single edge clears it
- **THEN** the model SHALL return a finite multi-edge UTD loss no less than the strongest single
  obstacle's contribution, where the single-wedge path alone would have produced no diffracted path

#### Scenario: Multi-edge loss is reciprocal
- **WHEN** the transmitter and receiver are swapped on a doubly-obstructed link
- **THEN** the multi-edge UTD loss SHALL be equal within numerical tolerance

