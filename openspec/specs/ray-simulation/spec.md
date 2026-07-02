# ray-simulation Specification

## Purpose
TBD - created by archiving change phase1-cpu-prototype. Update Purpose after archive.
## Requirements
### Requirement: Backend abstraction
The library SHALL define a backend interface that decouples ray traversal and bulk RF
computation from the simulation model, so that additional backends (Metal, CUDA, OpenCL)
can be added without changing scene, RF physics, or result code.

#### Scenario: Backend selection via settings
- **WHEN** `SimulationSettings.backend` names an available backend
- **THEN** the simulator SHALL run using that backend

#### Scenario: Unavailable backend falls back or errors
- **WHEN** a backend is requested that was not compiled in
- **THEN** the simulator SHALL either fall back to the CPU backend or report a clear error,
  as documented, and SHALL NOT crash

### Requirement: CPU backend
The library SHALL provide a CPU backend built on the NanoRT-style BVH that serves as the
correctness reference for all other backends.

#### Scenario: CPU backend runs a simulation
- **WHEN** a scene is simulated with `Backend::CPU`
- **THEN** the backend SHALL build the BVH, perform ray/occlusion queries, and produce RF
  paths and per-receiver results

### Requirement: Simulation settings
The library SHALL expose `SimulationSettings` controlling at least the backend, maximum
reflection bounces (`maxReflections`), rays per transmitter, and the receiver capture
radius, with documented defaults.

#### Scenario: Default settings are valid
- **WHEN** `SimulationSettings` is default-constructed
- **THEN** it SHALL produce a runnable configuration (CPU backend, a finite
  `maxReflections`, and a positive capture radius)

#### Scenario: maxReflections bounds bounce depth
- **WHEN** `maxReflections` is set to N
- **THEN** the simulator SHALL not generate reflection paths with more than N bounces

### Requirement: Point-receiver simulation mode
The library SHALL support a point-receiver mode that computes propagation from each
transmitter to each explicitly defined receiver.

#### Scenario: One transmitter to many receivers
- **WHEN** a scene has one transmitter and several receivers
- **THEN** the simulator SHALL produce results for every (transmitter, receiver) pair whose
  receiver is reached by at least one path

#### Scenario: Receiver with no reaching path
- **WHEN** a receiver is fully blocked from a transmitter (no LOS and no valid reflection)
- **THEN** the result SHALL record that receiver as having no received signal rather than
  omitting it silently

### Requirement: Line-of-sight path finding
The library SHALL determine line-of-sight visibility between a transmitter and a receiver
using an occlusion query, and produce a direct path when unobstructed.

#### Scenario: Unobstructed LOS produces a direct path
- **WHEN** the segment between transmitter and receiver is unoccluded
- **THEN** the simulator SHALL produce a LOS path with two points and FSPL-based received
  power

#### Scenario: Obstructed LOS produces no direct path
- **WHEN** geometry blocks the transmitter–receiver segment
- **THEN** the simulator SHALL not produce a LOS path for that pair

### Requirement: Specular reflection path finding
The library SHALL find specular reflection paths up to `maxReflections` bounces using the
image (mirror) method, validating each candidate path with occlusion queries for every
segment and confirming each reflection point lies on its reflecting triangle.

#### Scenario: Single-wall reflection is found
- **WHEN** a transmitter and receiver face a planar wall with a valid geometric reflection
- **THEN** the simulator SHALL produce a reflection path whose bounce point lies on the wall
  and whose segments are all unoccluded

#### Scenario: Invalid image point is rejected
- **WHEN** the mirror-image construction yields a reflection point outside the reflecting
  triangle, or any segment is occluded
- **THEN** that candidate reflection path SHALL be discarded

#### Scenario: Bounce count respected
- **WHEN** `maxReflections` is 1
- **THEN** only LOS and single-bounce paths SHALL be generated, and no two-bounce paths

### Requirement: Reproducible results
The library SHALL produce deterministic results for a fixed scene and fixed settings so
outputs are reproducible and comparable across runs and backends.

#### Scenario: Repeated runs match
- **WHEN** the same scene is simulated twice with identical settings on the CPU backend
- **THEN** the produced paths and per-receiver metrics SHALL be identical within
  floating-point tolerance

