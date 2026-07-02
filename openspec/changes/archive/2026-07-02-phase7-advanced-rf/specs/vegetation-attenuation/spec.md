## ADDED Requirements

### Requirement: Foliage attenuation model
The library SHALL compute vegetation (foliage) excess loss as a function of the in-foliage
path depth (m) and frequency, using a documented model (Weissberger's modified exponential
decay and/or ITU-R P.833), bounded to a physically reasonable maximum.

#### Scenario: Loss grows with foliage depth
- **WHEN** the path length through vegetation increases at a fixed frequency
- **THEN** the computed foliage loss SHALL increase (sub-linearly per Weissberger) up to the
  model's bound

#### Scenario: Zero depth, zero loss
- **WHEN** a path does not pass through any vegetation
- **THEN** the foliage loss SHALL be 0 dB

### Requirement: Vegetation traversal detection
The library SHALL determine the in-foliage segment length of a path from geometry whose
material is tagged as vegetation, and apply the foliage loss for that depth. (Explicit
vegetation volumes/boxes independent of mesh material are deferred to a follow-up.)

#### Scenario: Path crossing a tree canopy
- **WHEN** a path segment passes through geometry whose material is vegetation
- **THEN** the traversed depth SHALL be used as the foliage model input and the resulting loss
  subtracted from the path's received power
