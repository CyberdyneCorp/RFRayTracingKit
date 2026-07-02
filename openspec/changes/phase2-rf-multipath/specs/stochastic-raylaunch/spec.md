## ADDED Requirements

### Requirement: Ray-launch propagation
The library SHALL provide a stochastic propagation mode that launches
`raysPerTransmitter` rays distributed over the transmitter sphere, traces each through the
BVH, and reflects it specularly at surface hits up to `maxReflections` bounces.

#### Scenario: Rays are launched and traced
- **WHEN** a scene is simulated in ray-launch mode with N rays per transmitter
- **THEN** the engine SHALL trace N rays per transmitter, spawning a reflected ray at each
  surface hit until the bounce limit or the power floor is reached

#### Scenario: Even angular distribution
- **WHEN** rays are generated over the sphere
- **THEN** they SHALL be distributed approximately uniformly in solid angle (e.g. a
  Fibonacci sphere), not clustered at the poles

### Requirement: Receiver capture sphere
The library SHALL capture a traced ray as a path to a receiver when the ray passes within
the receiver's `captureRadius`, reconstructing the path geometry from the ray's bounce
history.

#### Scenario: Ray within capture radius is recorded
- **WHEN** a traced ray's segment passes within `captureRadius` of a receiver
- **THEN** a path SHALL be recorded from the transmitter through the ray's bounce points to
  the receiver, with reflection count and material hits from the bounce history

#### Scenario: Ray outside capture radius is ignored
- **WHEN** no segment of a traced ray comes within `captureRadius` of a receiver
- **THEN** no path SHALL be recorded for that ray/receiver pair

### Requirement: Path deduplication
The library SHALL deduplicate near-identical captured paths (same bounce surfaces and
similar geometry) so a receiver's aggregate is not inflated by many rays representing one
physical path.

#### Scenario: Duplicate rays collapse to one path
- **WHEN** many launched rays reach a receiver via the same sequence of reflecting surfaces
- **THEN** they SHALL be merged into a single representative path before aggregation

### Requirement: Agreement with the image-method reference
Stochastic ray-launch aggregate received power SHALL agree with the deterministic image
method on the Phase 1 golden scenes within a documented tolerance, given sufficient rays.

#### Scenario: Ray launch matches image method on a single-wall scene
- **WHEN** the single-wall golden scene is simulated by both methods with sufficient rays
- **THEN** the aggregate received power SHALL agree within the documented tolerance (e.g.
  ≤1 dB)

### Requirement: Deterministic seeding
Ray-launch results SHALL be reproducible for a fixed scene, settings, and RNG seed.

#### Scenario: Same seed reproduces results
- **WHEN** the same scene is simulated twice in ray-launch mode with the same seed
- **THEN** the captured paths and aggregate metrics SHALL be identical
