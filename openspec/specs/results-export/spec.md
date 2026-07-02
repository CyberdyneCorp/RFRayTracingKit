# results-export Specification

## Purpose
TBD - created by archiving change phase1-cpu-prototype. Update Purpose after archive.
## Requirements
### Requirement: Path result model
The library SHALL represent each propagation path with its transmitter id, receiver id,
ordered geometry points, received power (dBm), path loss (dB), phase (rad), delay (s),
reflection count, diffraction count, and the ordered material names it interacted with.

#### Scenario: LOS path is recorded
- **WHEN** a line-of-sight path is produced
- **THEN** its record SHALL contain exactly the transmitter and receiver points, zero
  reflections, and the corresponding power/loss/phase/delay

#### Scenario: Reflection path records bounce geometry and material
- **WHEN** a single-bounce reflection path is produced
- **THEN** its record SHALL contain three points, a reflection count of 1, and the name of
  the material at the bounce point

### Requirement: Per-receiver aggregated result
The library SHALL produce, for each receiver, an aggregated result containing the receiver
id and position, aggregated received power (dBm), path loss (dB), delay spread, and the
list of contributing paths.

#### Scenario: Delay spread from multipath
- **WHEN** multiple paths of differing delays reach a receiver
- **THEN** the aggregated result SHALL report a delay spread computed from the paths'
  delays and powers

#### Scenario: Aggregated power matches combining rule
- **WHEN** several paths reach a receiver
- **THEN** the aggregated received power SHALL equal the RF power-aggregation of the
  contributing paths' powers

### Requirement: JSON export
The library SHALL export the full result set to JSON, including simulation metadata,
transmitters, and per-receiver results with their paths.

#### Scenario: Export produces schema-conformant JSON
- **WHEN** results are exported to JSON
- **THEN** the file SHALL contain simulation id, frequency, transmitters, and an array of
  receivers each with received power, path loss, delay spread and a paths array

#### Scenario: Round-trip stability
- **WHEN** exported JSON is re-read by the library's result loader
- **THEN** the reconstructed results SHALL match the originals within documented tolerance

### Requirement: CSV export
The library SHALL export a per-receiver summary table to CSV suitable for spreadsheets and
data-science tools.

#### Scenario: One row per receiver
- **WHEN** results are exported to CSV
- **THEN** the file SHALL contain a header row and one row per receiver with at least
  receiver id, position, received power (dBm), path loss (dB) and delay spread

#### Scenario: Receiver with no signal
- **WHEN** a receiver received no path
- **THEN** its CSV row SHALL represent the absence of signal with a documented sentinel
  (e.g. empty power/loss fields) rather than being omitted

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

### Requirement: SINR in receiver results
Per-receiver results SHALL, when SINR is enabled, include the serving transmitter id, the SINR
(dB), and the interference power, in addition to the existing metrics.

#### Scenario: Serving cell and SINR present
- **WHEN** a multi-transmitter simulation with SINR enabled completes
- **THEN** each reached receiver's result SHALL carry a serving transmitter id and an SINR value

### Requirement: MIMO and route result export
The library SHALL export the MIMO channel matrix (and capacity) for an array pair, and export a
route result series (JSON and CSV) in sample order.

#### Scenario: Route CSV in order
- **WHEN** a route result is exported to CSV
- **THEN** rows SHALL appear in route-sample order with position and metrics per row

#### Scenario: MIMO matrix export
- **WHEN** a MIMO result is exported
- **THEN** the output SHALL contain the complex channel matrix dimensions and entries plus the
  capacity estimate

