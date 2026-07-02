## ADDED Requirements

### Requirement: Polarization mismatch in the received-power budget
The per-path received-power budget SHALL, via the existing budget hook, subtract a polarization
mismatch loss derived from the arriving path polarization and the receiver antenna polarization,
defaulting to co-polar so the term is 0 dB and the archived budget is unchanged.

#### Scenario: Co-polar default preserves the budget
- **WHEN** a path is evaluated with default (co-polar) transmitter/receiver polarization
- **THEN** the polarization mismatch term SHALL be 0 dB and the received power SHALL equal the
  archived Phase 1/2/7 budget bit-for-bit

#### Scenario: Cross-polar receiver reduces received power
- **WHEN** the receiver antenna polarization is orthogonal to the arriving path polarization
- **THEN** the received power SHALL be reduced by the polarization mismatch loss for that pair

#### Scenario: Extended budget composition with polarization
- **WHEN** a path is evaluated with the advanced-loss terms and a non-co-polar polarization
- **THEN** received power SHALL equal Ptx + Gtx + Grx − (FSPL + reflection + penetration +
  diffraction + atmospheric + vegetation + polarization mismatch) losses

### Requirement: Per-path Doppler shift on results
Each path SHALL carry a Doppler frequency shift `dopplerHz`, computed from the receiver velocity
and the path arrival direction, defaulting to 0 for static receivers so archived results are
unchanged.

#### Scenario: Static receiver reports zero Doppler
- **WHEN** received power and metadata are computed for a path to a static receiver
- **THEN** the path's `dopplerHz` SHALL be exactly 0 and the archived result SHALL be unchanged

#### Scenario: Moving receiver reports a Doppler shift
- **WHEN** the receiver has a non-zero velocity with a component along a path's arrival direction
- **THEN** the path's `dopplerHz` SHALL equal `(v·khat)/c·f` with a closing receiver giving a
  positive shift
