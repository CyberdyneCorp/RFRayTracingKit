# route-simulation Specification

## Purpose
TBD - created by archiving change phase7-advanced-rf. Update Purpose after archive.
## Requirements
### Requirement: Route definition
The library SHALL represent a receiver route as an ordered list of waypoints (positions) with
an optional sample spacing or per-waypoint timestamps, producing a sequence of receiver
positions to evaluate.

#### Scenario: Sampled along a polyline
- **WHEN** a route is defined by waypoints and a sample spacing s
- **THEN** the library SHALL generate receiver sample positions spaced ~s along the polyline

### Requirement: Route simulation mode
The library SHALL evaluate a moving receiver at each route sample, producing an ordered series
of per-sample results (position and the usual received-power/path-loss/delay-spread metrics).

#### Scenario: Result series matches samples
- **WHEN** a route with K samples is simulated for a transmitter
- **THEN** the result SHALL contain K ordered entries, each with the sample position and its
  RF metrics

#### Scenario: Drive-test export
- **WHEN** a route result is exported to CSV
- **THEN** the file SHALL contain one row per sample with position and metrics (drive-test
  style), in route order

