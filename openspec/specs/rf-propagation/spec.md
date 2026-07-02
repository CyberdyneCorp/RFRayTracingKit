# rf-propagation Specification

## Purpose
TBD - created by archiving change phase1-cpu-prototype. Update Purpose after archive.
## Requirements
### Requirement: Free-space path loss
The library SHALL compute free-space path loss (FSPL) in dB as a function of distance and
frequency, using the standard formula
`FSPL(dB) = 20·log10(d) + 20·log10(f) + 20·log10(4π/c)`.

#### Scenario: Known FSPL reference value
- **WHEN** FSPL is computed for 1 km at 3.5 GHz
- **THEN** the result SHALL equal the analytic value (≈103.3 dB) within a documented
  tolerance (≤0.1 dB)

#### Scenario: Distance and frequency scaling
- **WHEN** either the distance or the frequency is doubled
- **THEN** the computed FSPL SHALL increase by approximately 6.02 dB

#### Scenario: Guard for zero distance
- **WHEN** FSPL is requested for a non-positive distance
- **THEN** the library SHALL return a documented sentinel or clamp rather than producing
  `-inf`/NaN

### Requirement: Reflection loss
The library SHALL compute the reflection loss applied when a ray specularly reflects off a
surface, derived from Fresnel reflection coefficients for the material's electromagnetic
parameters, polarization and incidence angle, and SHALL fall back to the material's
configured `reflectionLossDb` when Fresnel parameters are unavailable.

#### Scenario: Grazing vs normal incidence
- **WHEN** reflection loss is computed for the same material at near-grazing and near-normal
  incidence
- **THEN** the two SHALL differ in accordance with the Fresnel equations for the given
  polarization

#### Scenario: Constant-loss fallback
- **WHEN** a material provides only a `reflectionLossDb` value and no valid permittivity
- **THEN** the library SHALL apply that constant reflection loss

### Requirement: Fresnel coefficients
The library SHALL provide Fresnel reflection coefficient computation for perpendicular
(TE) and parallel (TM) polarizations from complex relative permittivity and incidence
angle.

#### Scenario: Perfect conductor limit
- **WHEN** conductivity tends to infinity (perfect electric conductor)
- **THEN** the magnitude of the reflection coefficient SHALL approach 1 (near-total
  reflection)

#### Scenario: Polarization selection
- **WHEN** a coefficient is requested for TE versus TM polarization at oblique incidence
- **THEN** the library SHALL return the corresponding Fresnel value for that polarization

### Requirement: Material penetration loss
The library SHALL apply a material-dependent penetration (transmission) loss in dB when a
ray passes through a surface.

#### Scenario: Penetration attenuates power
- **WHEN** a path transmits through a material with a positive `penetrationLossDb`
- **THEN** the received power along that path SHALL be reduced by that penetration loss

### Requirement: Phase accumulation
The library SHALL accumulate the propagation phase along a path as
`φ = (2π·f/c)·pathLength`, plus any reflection phase contributions, expressed in radians.

#### Scenario: Phase from path length
- **WHEN** phase is computed for a straight path of length equal to one wavelength
- **THEN** the accumulated propagation phase SHALL equal 2π radians (modulo 2π) within
  tolerance

### Requirement: Propagation delay
The library SHALL compute propagation delay as `τ = pathLength / c` in seconds.

#### Scenario: Delay for a known path length
- **WHEN** delay is computed for a 300 m path
- **THEN** the result SHALL be ≈1.0 µs within tolerance

### Requirement: Antenna gain application
The library SHALL apply transmitter and receiver antenna gains (dBi) to a path based on the
departure and arrival directions and each antenna's pattern.

#### Scenario: Omnidirectional link budget
- **WHEN** both antennas are omnidirectional with 0 dBi gain
- **THEN** no antenna gain term SHALL modify the received power beyond path loss

#### Scenario: Directional gain in link budget
- **WHEN** a directional transmitter antenna is oriented toward the departure direction
- **THEN** the corresponding gain SHALL be added to the path's received power

### Requirement: Per-path received power
The library SHALL compute the received power of a single path as
`Prx = Ptx + Gtx + Grx − (FSPL + Σ reflection losses + Σ penetration losses)`, all in dB/dBm.

#### Scenario: LOS link budget
- **WHEN** received power is computed for a line-of-sight path
- **THEN** it SHALL equal transmit power plus antenna gains minus FSPL, with no reflection
  or penetration terms

#### Scenario: Reflected path adds reflection loss
- **WHEN** received power is computed for a single-bounce reflected path
- **THEN** it SHALL additionally subtract the reflection loss at the bounce surface

### Requirement: Multi-path power aggregation
The library SHALL aggregate the contributions of multiple paths reaching a receiver into a
single received power, supporting both incoherent (power sum) and coherent (phase-aware
complex sum) combining.

#### Scenario: Incoherent aggregation
- **WHEN** several paths are combined incoherently
- **THEN** the aggregate SHALL be `10·log10(Σ 10^(Pi/10))` over the per-path powers `Pi`

#### Scenario: Coherent aggregation uses phase
- **WHEN** paths are combined coherently
- **THEN** the aggregate SHALL sum complex amplitudes using each path's phase, so paths can
  constructively or destructively interfere

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

