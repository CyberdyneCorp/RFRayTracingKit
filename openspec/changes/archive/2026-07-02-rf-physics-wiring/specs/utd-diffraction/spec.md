## ADDED Requirements

### Requirement: UTD as a selectable diffraction path model
The simulator SHALL offer UTD as a selectable diffraction model (`DiffractionModel::UTD`) whose
diffracted-path loss is computed from the UTD conducting-half-plane wedge coefficient, with the
knife-edge model remaining the default so archived results are unchanged.

#### Scenario: UTD selected produces a diffracted path
- **WHEN** a link's line of sight is blocked, diffraction is enabled, and the UTD model is selected
- **THEN** the simulator SHALL produce a diffraction path whose loss is finite and computed via the
  UTD coefficient

#### Scenario: UTD tracks the knife-edge loss
- **WHEN** the UTD half-plane diffraction loss is evaluated across the Fresnel parameter v
- **THEN** it SHALL match the ITU-R knife-edge loss within a documented tolerance (≈6 dB at v=0)
