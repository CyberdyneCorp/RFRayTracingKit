## ADDED Requirements

### Requirement: Extended per-path loss budget
The per-path received-power budget SHALL, when the corresponding features are enabled, add
diffraction loss, rain + gaseous atmospheric attenuation, and vegetation (foliage) loss to
the existing FSPL + reflection + penetration terms.

#### Scenario: Extended budget composition
- **WHEN** a path is evaluated with diffraction, atmospheric, and vegetation losses enabled
- **THEN** received power SHALL equal Ptx + Gtx + Grx − (FSPL + reflection + penetration +
  diffraction + atmospheric + vegetation) losses

#### Scenario: Additive terms are zero when disabled
- **WHEN** the advanced-loss features are disabled
- **THEN** the extra loss terms SHALL contribute 0 dB and the budget SHALL equal the Phase 1/2
  budget
