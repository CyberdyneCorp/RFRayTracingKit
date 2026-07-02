## ADDED Requirements

### Requirement: Coverage result model
The library SHALL represent a coverage result with a 2D array of received power (dBm), the
grid georeference (origin, cell size, columns, rows, height), and an optional matching path
-loss array.

#### Scenario: Coverage result exposes its array and grid
- **WHEN** a coverage-grid simulation completes
- **THEN** the result SHALL expose an H×W power array and the grid metadata used to produce
  it

### Requirement: Coverage JSON export
The library SHALL export a coverage result to JSON, including the grid metadata and the
power array.

#### Scenario: Coverage JSON contains grid and values
- **WHEN** a coverage result is exported to JSON
- **THEN** the file SHALL contain the grid origin, cell size, dimensions, height, and the
  row-major power values

### Requirement: Coverage CSV export
The library SHALL export a coverage result to CSV as a grid of power values (or a long
`row,col,x,y,power` table), with a documented no-signal sentinel.

#### Scenario: Coverage CSV has one value per cell
- **WHEN** a coverage result is exported to CSV
- **THEN** the file SHALL contain one power value per grid cell, and no-signal cells SHALL
  use the documented sentinel
