## ADDED Requirements

### Requirement: Jones-vector path polarization
The library SHALL represent a path's polarization as a Jones vector of two complex components in
a transverse basis, with canonical states for Vertical, Horizontal, RHCP, and LHCP, and SHALL
track a per-path polarization on `RFPath`.

#### Scenario: Canonical states are represented
- **WHEN** a polarization is constructed for Vertical, Horizontal, RHCP, or LHCP
- **THEN** it SHALL be a two-component complex Jones vector, with the circular states equal to
  `(1, ∓j)/√2` in the transverse basis

#### Scenario: Per-path polarization defaults co-polar
- **WHEN** a path is produced without an explicit polarization override
- **THEN** its tracked polarization SHALL default to the transmitter polarization (co-polar)

### Requirement: Polarization mismatch loss
The library SHALL provide `polarizationMismatchDb(txPol, rxPol)` returning the polarization
mismatch loss in dB from the normalized inner product of the two Jones vectors, so that co-polar
antennas incur 0 dB, a 45° linear offset incurs ≈3.01 dB, and orthogonal polarizations incur a
large loss clamped to a documented sentinel.

#### Scenario: Co-polar is lossless
- **WHEN** transmitter and receiver polarizations are identical
- **THEN** the mismatch loss SHALL be 0 dB within a documented tolerance

#### Scenario: 45-degree offset reference value
- **WHEN** the receiver polarization is a 45° linear rotation of the transmitter polarization
- **THEN** the mismatch loss SHALL be ≈3.01 dB within a documented tolerance

#### Scenario: Orthogonal polarizations
- **WHEN** the polarizations are orthogonal (e.g. Vertical vs Horizontal, or RHCP vs LHCP)
- **THEN** the mismatch loss SHALL be a large value clamped to a documented sentinel rather than
  producing +inf or NaN

### Requirement: Depolarizing reflection
When a path reflects off a surface, the library SHALL depolarize its Jones vector by decomposing
it into TE (perpendicular) and TM (parallel) components relative to the plane of incidence,
applying the corresponding complex Fresnel reflection coefficients, and recomposing the vector.

#### Scenario: Reflection alters polarization per Fresnel
- **WHEN** a path with a known incident polarization reflects off a material at oblique incidence
- **THEN** the resulting Jones vector SHALL equal the incident TE/TM components scaled by the
  material's TE/TM Fresnel coefficients, so the polarization is rotated/ellipticized accordingly

### Requirement: Polarization mismatch in the per-path budget
The per-path received-power budget SHALL add the polarization mismatch term via the existing
budget hook, defaulting to co-polar so it contributes 0 dB and archived results are unchanged.

#### Scenario: Default co-polar leaves the budget unchanged
- **WHEN** a path is evaluated with default (co-polar) polarization
- **THEN** the polarization mismatch term SHALL be 0 dB and the received power SHALL equal the
  archived budget bit-for-bit

#### Scenario: Cross-polar receiver loses power
- **WHEN** the receiver antenna polarization is orthogonal to the arriving path polarization
- **THEN** the received power SHALL be reduced by the polarization mismatch loss
