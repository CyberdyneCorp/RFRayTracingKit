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
The library SHALL export a coverage result to CSV as a long table with the header
`row,col,x,y,power` (one row per grid cell), using a documented no-signal sentinel for
cells with no path.

#### Scenario: Coverage CSV is a long per-cell table
- **WHEN** a coverage result is exported to CSV
- **THEN** the file SHALL have a `row,col,x,y,power` header and one data row per grid cell,
  and no-signal cells SHALL carry the documented sentinel in the `power` column
