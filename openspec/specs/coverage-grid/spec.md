# coverage-grid Specification

## Purpose
TBD - created by archiving change phase2-rf-multipath. Update Purpose after archive.
## Requirements
### Requirement: Coverage grid definition
The library SHALL let a caller define a regular horizontal coverage grid by its origin,
cell size, column/row counts, and evaluation height, over which received power is computed.

#### Scenario: Grid is defined and sized
- **WHEN** a coverage grid is defined with W columns, H rows, cell size s, and height z
- **THEN** the grid SHALL enumerate W×H evaluation points at height z spaced s apart from
  the origin

### Requirement: Coverage grid simulation mode
The library SHALL provide a coverage-grid simulation mode that evaluates received power and
path loss at each grid cell, treating each cell centre as a receiver.

#### Scenario: Coverage array is produced
- **WHEN** a scene with at least one transmitter is simulated in coverage-grid mode
- **THEN** the result SHALL contain a 2D array of received power (dBm) sized H×W aligned to
  the grid

#### Scenario: Cell with no signal is marked
- **WHEN** a grid cell is not reached by any path
- **THEN** its coverage value SHALL be a documented no-signal sentinel (e.g. -inf or NaN),
  distinguishable from a very low but valid power

### Requirement: Coverage grid georeference
The coverage result SHALL retain the grid's georeference (origin, cell size, dimensions,
height) so exporters can place the array in space.

#### Scenario: Georeference round-trips to export
- **WHEN** a coverage result is exported and its metadata inspected
- **THEN** the origin, cell size, and dimensions SHALL match the grid that was simulated

