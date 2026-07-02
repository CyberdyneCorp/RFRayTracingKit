# diffraction Specification

## Purpose
TBD - created by archiving change phase7-advanced-rf. Update Purpose after archive.
## Requirements
### Requirement: Knife-edge diffraction loss
The library SHALL compute single knife-edge diffraction loss (dB) from the Fresnel-Kirchhoff
diffraction parameter v, using the standard ITU-R P.526 approximation.

#### Scenario: Grazing edge reference value
- **WHEN** diffraction loss is computed for v = 0 (ray grazing the edge)
- **THEN** the loss SHALL be approximately 6 dB within a documented tolerance

#### Scenario: Loss increases into shadow
- **WHEN** the receiver moves deeper into the geometric shadow (increasing v)
- **THEN** the computed diffraction loss SHALL increase monotonically

#### Scenario: Clear line of sight
- **WHEN** the direct ray clears the edge with ample Fresnel clearance (v strongly negative)
- **THEN** the diffraction loss SHALL approach 0 dB

### Requirement: Diffracted path finding
The library SHALL, when `SimulationSettings.enableDiffraction` is set, generate diffracted
paths over obstacle edges between a transmitter and receiver whose direct path is blocked,
recording each as a path of type `diffraction` with a diffraction count.

#### Scenario: Shadowed receiver gains a diffracted path
- **WHEN** the LOS between tx and rx is blocked by an edge and diffraction is enabled
- **THEN** the simulator SHALL produce at least one diffraction path whose received power
  reflects the knife-edge loss over that edge

#### Scenario: Diffraction disabled
- **WHEN** `enableDiffraction` is false
- **THEN** no diffraction paths SHALL be produced (Phase 1/2 behavior is preserved)

