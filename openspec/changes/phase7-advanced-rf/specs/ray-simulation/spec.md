## ADDED Requirements

### Requirement: Diffraction and advanced-loss simulation settings
`SimulationSettings` SHALL expose `enableDiffraction`, and toggles/parameters for atmospheric
(rain rate), vegetation, and SINR/noise-floor behavior, all defaulting off so Phase 1/2/4
results are unchanged.

#### Scenario: Defaults preserve prior behavior
- **WHEN** `SimulationSettings` is default-constructed
- **THEN** diffraction, rain, vegetation, and SINR features SHALL be disabled and results
  SHALL match the archived Phase 1/2 behavior

### Requirement: Route simulation mode
The simulator SHALL support a route (moving-receiver) mode that evaluates a receiver route and
returns an ordered per-sample result series.

#### Scenario: Route mode returns a series
- **WHEN** the simulator is run over a defined route
- **THEN** it SHALL return one result per route sample, in order
