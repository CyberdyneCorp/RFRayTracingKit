## ADDED Requirements

### Requirement: Scene container
The library SHALL provide a `Scene` type that holds a mesh collection, a material table,
transmitters, receivers, and a coordinate system, forming the single backend-agnostic
input to a simulation.

#### Scenario: Construct an empty scene
- **WHEN** a `Scene` is default-constructed
- **THEN** it SHALL contain no meshes, materials, transmitters or receivers and SHALL be
  a valid (if empty) simulation input

#### Scenario: Assemble a scene programmatically
- **WHEN** meshes, materials, at least one transmitter and at least one receiver are added
- **THEN** the `Scene` SHALL expose them for iteration by the simulator and exporters

### Requirement: Transmitter definition
The library SHALL represent a transmitter with a unique id, position, frequency in Hz,
transmit power in dBm, an antenna pattern, and a polarization.

#### Scenario: Add a transmitter
- **WHEN** a transmitter is added with id, position, `frequencyHz` and `powerDbm`
- **THEN** the scene SHALL store it and default the antenna pattern to omnidirectional when
  none is supplied

#### Scenario: Duplicate transmitter id is rejected
- **WHEN** a transmitter is added whose id already exists in the scene
- **THEN** the library SHALL reject the addition with a reported error

### Requirement: Receiver definition
The library SHALL represent a receiver with a unique id, position, an antenna pattern, and
a polarization.

#### Scenario: Add a receiver
- **WHEN** a receiver is added with id and position
- **THEN** the scene SHALL store it and default the antenna pattern to omnidirectional when
  none is supplied

#### Scenario: Duplicate receiver id is rejected
- **WHEN** a receiver is added whose id already exists in the scene
- **THEN** the library SHALL reject the addition with a reported error

### Requirement: Material definition
The library SHALL represent an RF material with a name and the electromagnetic parameters
relative permittivity, conductivity, roughness, penetration loss (dB), and reflection loss
(dB).

#### Scenario: Define a material
- **WHEN** a material is created with name and its electromagnetic parameters
- **THEN** the scene SHALL store it and make it addressable by name for assignment to meshes

#### Scenario: Built-in material presets
- **WHEN** application code requests a common preset (e.g. concrete, brick, glass, metal,
  wood, water, vegetation, asphalt, soil)
- **THEN** the library SHALL provide default electromagnetic parameters for that material

### Requirement: Mesh collection with per-triangle material
The library SHALL store scene geometry as a collection of triangle meshes, each associated
with a material, so that ray hits can resolve the material at the intersection point.

#### Scenario: Resolve material from a hit
- **WHEN** a ray query returns a triangle index
- **THEN** the scene SHALL resolve the owning mesh and its assigned material for that
  triangle

#### Scenario: Mesh with no assigned material
- **WHEN** a mesh has no explicit material assignment
- **THEN** the library SHALL apply a documented default material rather than failing

### Requirement: Antenna pattern
The library SHALL represent an antenna pattern that returns a gain in dBi for a given
direction relative to the antenna orientation, and SHALL support at least an
omnidirectional pattern.

#### Scenario: Omnidirectional gain
- **WHEN** an omnidirectional pattern is queried for any direction
- **THEN** it SHALL return a constant gain (default 0 dBi unless configured)

#### Scenario: Directional gain lookup
- **WHEN** a directional pattern is queried with an azimuth/elevation direction
- **THEN** it SHALL return the gain for that direction, interpolating between tabulated
  samples where applicable

### Requirement: Coordinate system
The library SHALL record the scene's coordinate system so positions have a defined meaning
and exporters can annotate output accordingly. The core frame SHALL be right-handed with
Z as the up (height/elevation) axis.

#### Scenario: Local Cartesian scene
- **WHEN** a scene is created without geographic georeferencing
- **THEN** the coordinate system SHALL default to a local right-handed Z-up Cartesian frame
  in metres

#### Scenario: Height is the Z component
- **WHEN** a transmitter is placed at height h above the ground plane
- **THEN** its position's Z component SHALL equal h, consistent with imported geometry
