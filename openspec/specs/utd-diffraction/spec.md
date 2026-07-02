# utd-diffraction Specification

## Purpose
TBD - created by archiving change rf-physics-advanced. Update Purpose after archive.
## Requirements
### Requirement: UTD Fresnel transition function
The library SHALL provide the complex Fresnel transition function `utdTransition(x)` used by the
Kouyoumjian–Pathak uniform theory of diffraction, accepting a complex argument and returning a
complex value, using the documented small-argument and large-argument asymptotic forms.

#### Scenario: Transition tends to unity for large argument
- **WHEN** `utdTransition(x)` is evaluated for a large positive real argument (deep in the lit or
  shadow region)
- **THEN** the result SHALL approach 1 + 0j within a documented tolerance

#### Scenario: Transition reference value
- **WHEN** `utdTransition(x)` is evaluated at a small reference argument
- **THEN** its magnitude and phase SHALL match the published UTD transition-function value within
  a documented tolerance

### Requirement: UTD wedge diffraction coefficient
The library SHALL provide a Kouyoumjian–Pathak wedge diffraction coefficient
`D(phi, phiPrime, beta0, wedgeN, k)` combining the four cotangent terms, each weighted by the
Fresnel transition function, with the standard `1/(2n·sqrt(2πk)·sin β0)` prefactor, where
`wedgeN` is the exterior wedge factor (n = exterior wedge angle / π).

#### Scenario: Half-plane recovers knife-edge behavior
- **WHEN** the wedge coefficient is evaluated for a half-plane (`wedgeN` = 2) across the
  shadow boundary
- **THEN** its behavior SHALL match the knife-edge diffraction shadow-boundary transition within
  a documented tolerance

### Requirement: UTD coefficient is finite on boundaries
The UTD wedge coefficient SHALL return finite values everywhere in its domain, including exactly
on the incident/reflection shadow boundaries (where the cotangent pole and the transition-function
zero cancel) and at grazing skew (sin β0 → 0).

#### Scenario: Finite on a shadow boundary
- **WHEN** the coefficient is evaluated exactly on the incident or reflection shadow boundary
- **THEN** the returned value SHALL be finite (no NaN/inf)

#### Scenario: Finite at grazing skew
- **WHEN** the coefficient is evaluated with β0 → 0
- **THEN** the returned value SHALL be finite

<!-- NOTE: The UTD primitives above (transition function + wedge coefficient) are the delivered,
     analytically-validated scope of this change. Wiring UTD in as a third selectable diffraction
     PATH model in SimulationSettings — which requires per-edge wedge-geometry extraction and
     spreading-factor normalization for arbitrary building edges — is a documented follow-up
     (`DiffractionModel::UTD`). The knife-edge and multi-edge (Bullington/Deygout) models remain
     the selectable simulation models; knife-edge stays the default. -->

### Requirement: Knife-edge remains the default diffraction model
The default diffraction model SHALL remain the knife-edge model so that default-constructed
settings reproduce prior results; no UTD computation SHALL affect default results.

#### Scenario: Knife-edge remains default
- **WHEN** `SimulationSettings` is default-constructed
- **THEN** the diffraction model SHALL be the knife-edge model and no UTD computation SHALL affect
  the results

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

