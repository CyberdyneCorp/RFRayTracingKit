# doppler Specification

## Purpose
TBD - created by archiving change rf-physics-advanced. Update Purpose after archive.
## Requirements
### Requirement: Per-path Doppler shift
The library SHALL provide `perPathDopplerHz(receiverVelocity, arrivalDirUnit, freq)` computing the
Doppler frequency shift as `(v · khat)/c · f`, where `khat` is the unit vector from the receiver
toward the last hop (the path's arrival direction) and the sign convention is that a closing
receiver produces a positive shift.

#### Scenario: Closing receiver reference value
- **WHEN** a receiver closes at speed v straight along a line-of-sight arrival direction at
  frequency f
- **THEN** the Doppler shift SHALL equal `+v/c·f` within a documented tolerance

#### Scenario: Transverse motion produces no shift
- **WHEN** the receiver velocity is perpendicular to the path arrival direction
- **THEN** the Doppler shift SHALL be 0 Hz within a documented tolerance

#### Scenario: Receding receiver is negative
- **WHEN** the receiver recedes along the arrival direction
- **THEN** the Doppler shift SHALL be negative

### Requirement: Doppler field on paths and route samples
The library SHALL expose a `dopplerHz` field on `RFPath` and on `RouteSample`, defaulting to 0, so
that static receivers and existing results are unchanged.

#### Scenario: Static receiver yields zero Doppler
- **WHEN** a path is produced for a receiver with zero velocity
- **THEN** its `dopplerHz` SHALL be exactly 0 and archived results SHALL be unchanged

### Requirement: Route simulator fills per-path Doppler
The route simulator SHALL derive the receiver velocity from consecutive route samples (direction
from sample-to-sample displacement, magnitude from spacing or an optional configured speed) and
SHALL fill each path's `dopplerHz` accordingly.

#### Scenario: Moving route produces non-zero Doppler
- **WHEN** a route has the receiver moving toward a transmitter between consecutive samples
- **THEN** the paths at the moving sample SHALL carry a non-zero `dopplerHz` consistent with the
  derived receiver velocity

#### Scenario: Single-sample or stationary route
- **WHEN** a route sample has no derivable velocity (single sample or coincident neighbors)
- **THEN** the per-path `dopplerHz` SHALL be 0

