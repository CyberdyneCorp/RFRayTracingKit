## MODIFIED Requirements

### Requirement: UTD as a selectable diffraction path model
The simulator SHALL offer UTD as a selectable diffraction model (`DiffractionModel::UTD`) whose
diffracted-path loss is computed from the Kouyoumjian–Pathak wedge coefficient using the diffracting
edge's **extracted wedge geometry** (wedge factor `n`, incidence/diffraction/skew angles) and the UTD
distance parameter and spherical spreading factor. For a half-plane edge (`n = 2`) the loss SHALL
reduce to the ITU-R knife-edge loss within a documented tolerance, so the prior UTD-tracks-knife-edge
behaviour is preserved as a limiting case. The knife-edge model SHALL remain the default so
default-constructed settings and archived non-UTD results are unchanged.

#### Scenario: UTD selected produces a diffracted path
- **WHEN** a link's line of sight is blocked, diffraction is enabled, and the UTD model is selected
- **THEN** the simulator SHALL produce a diffraction path whose loss is finite and computed via the
  UTD wedge coefficient with the edge's extracted wedge geometry

#### Scenario: UTD reduces to knife-edge for a half-plane edge
- **WHEN** the diffracting edge is a conducting half-plane (`n = 2`) and the UTD half-plane diffraction
  loss is evaluated across the Fresnel parameter v
- **THEN** it SHALL match the ITU-R knife-edge loss within a documented tolerance (≈6 dB at v=0)
