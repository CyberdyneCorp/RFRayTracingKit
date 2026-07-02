## ADDED Requirements

### Requirement: Reflection depolarization on the reflected path
When `SimulationSettings.enableDepolarization` is set, the reflection path builder SHALL apply each
bounce's complex TE/TM Fresnel coefficients to the path's Jones polarization state, so an incident
wave with both transverse components becomes elliptical when TE and TM differ, increasing the
polarization-mismatch loss. The setting SHALL default off, leaving co-polar/archived results unchanged.

#### Scenario: Depolarization is opt-in and default-neutral
- **WHEN** `enableDepolarization` is false (default)
- **THEN** a reflected path's polarization-mismatch loss SHALL be unchanged from the archived budget
  (0 dB for a co-polar link)

#### Scenario: A reflected circular wave depolarizes
- **WHEN** `enableDepolarization` is true and a circularly-polarized link reflects off a surface with
  unequal TE/TM coefficients
- **THEN** the reflected path's received power SHALL be lower than with depolarization off (nonzero
  mismatch), without double-counting the reflection amplitude loss
