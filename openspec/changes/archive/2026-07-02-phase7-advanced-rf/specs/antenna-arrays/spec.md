## ADDED Requirements

### Requirement: Antenna array geometry
The library SHALL represent an antenna array as a set of element positions (e.g. uniform
linear or planar array with configurable spacing) attached to a transmitter or receiver.

#### Scenario: Uniform linear array construction
- **WHEN** a ULA of N elements at spacing d is created
- **THEN** the array SHALL expose N element positions spaced d apart along its axis

### Requirement: Array factor and beam steering
The library SHALL compute the array factor gain toward a direction given per-element phase
weights, and SHALL provide beam steering that sets those weights to point the main beam at a
target direction.

#### Scenario: Steered main beam peaks at the target
- **WHEN** the array is steered toward a direction θ0 and the array factor is evaluated at θ0
- **THEN** the array-factor gain SHALL be at its maximum (coherent element sum)

#### Scenario: Null/roll-off away from the beam
- **WHEN** the array factor is evaluated well away from the steered direction
- **THEN** the gain SHALL be lower than at the main-beam direction

### Requirement: Array gain in the link budget
The library SHALL let an array's steered gain replace or augment the single-element antenna
gain term for a transmitter/receiver in the per-path budget.

#### Scenario: Array gain applied to a path
- **WHEN** a transmitter uses a steered array and a path departs toward the beam
- **THEN** the path's received power SHALL include the array-factor gain in that direction
