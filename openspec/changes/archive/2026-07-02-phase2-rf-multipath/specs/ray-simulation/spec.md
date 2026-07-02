## ADDED Requirements

### Requirement: Propagation mode selection
The library SHALL let `SimulationSettings` select the propagation method: the deterministic
image method (Phase 1 default) or stochastic ray launch.

#### Scenario: Image method selected
- **WHEN** the propagation mode is set to image-method
- **THEN** the simulator SHALL produce paths via the deterministic image method as in
  Phase 1

#### Scenario: Ray-launch method selected
- **WHEN** the propagation mode is set to ray-launch
- **THEN** the simulator SHALL produce paths via the stochastic ray-launch engine with the
  receiver capture sphere

### Requirement: First-class multi-bounce reflections
The library SHALL support and validate reflection depth greater than one bounce in both
propagation modes, bounded by `maxReflections`.

#### Scenario: Two-bounce path is found
- **WHEN** a scene admits a valid two-bounce specular path and `maxReflections` ≥ 2
- **THEN** the simulator SHALL produce a path with two reflection points and reflection
  count 2

#### Scenario: Depth is still bounded
- **WHEN** `maxReflections` is N
- **THEN** no produced path SHALL exceed N reflections in either mode
