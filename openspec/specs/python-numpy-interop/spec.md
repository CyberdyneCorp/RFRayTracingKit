# python-numpy-interop Specification

## Purpose
TBD - created by archiving change phase3-python-bindings. Update Purpose after archive.
## Requirements
### Requirement: Zero-copy numpy result arrays
The Python `Result` SHALL expose large result buffers as numpy arrays via the buffer
protocol / `py::array_t` without copying where practical: `receiver_positions` as
float64[N,3], `received_power_dbm` as float64[N], and `path_loss_db` as float64[N].

#### Scenario: Receiver arrays have the documented dtype and shape
- **WHEN** a caller reads `result.receiver_positions`, `result.received_power_dbm`, and
  `result.path_loss_db` for a result with N receivers
- **THEN** the arrays SHALL be numpy float64 with shapes [N,3], [N], and [N] respectively,
  and their values SHALL match the corresponding per-receiver scalars

### Requirement: Coverage array as numpy
The coverage result SHALL expose its power grid as a numpy float64 array of shape [H,W]
aligned to the coverage grid, using the documented no-signal sentinel for unreached cells.

#### Scenario: Coverage array shape and sentinel
- **WHEN** a caller reads `coverage.coverage_array` for a grid of H rows and W columns
- **THEN** the array SHALL be numpy float64 of shape [H,W], and cells not reached by any
  path SHALL carry the documented no-signal sentinel distinguishable from valid low power

### Requirement: Path geometry as points plus offsets
The Python `Result` SHALL expose ray-path geometry as a flat numpy `points` array of
float64[M,3] together with an int32 `offsets` array of length P+1 (CSR-style), so path `p`
occupies `points[offsets[p]:offsets[p+1]]`.

#### Scenario: Offsets slice points into polylines
- **WHEN** a caller reads `result.path_points` and `result.path_offsets` for P paths
- **THEN** `path_offsets` SHALL have length P+1 with `path_offsets[0] == 0` and
  `path_offsets[P] == M`, and slicing `path_points` by consecutive offsets SHALL yield each
  path's ordered vertices

### Requirement: Array lifetime safety
Numpy arrays returned from a `Result` SHALL remain valid for use even after the originating
result object is garbage-collected, via keep-alive semantics or an owning copy, so callers
never read freed C++ memory.

#### Scenario: Array outlives its result
- **WHEN** a caller obtains an array from a result, deletes the result, then reads the array
- **THEN** the read SHALL return the same values without crashing

### Requirement: Pandas dataframes with lazy import
The Python `Result` SHALL provide `receivers_dataframe()` and `paths_dataframe()` that build
pandas DataFrames, importing pandas lazily so that a bare `import rftracekit` does not
require pandas; if pandas is missing, calling these methods SHALL raise a clear error naming
pandas.

#### Scenario: Receivers dataframe is produced when pandas is present
- **WHEN** pandas is installed and a caller calls `result.receivers_dataframe()`
- **THEN** a pandas DataFrame SHALL be returned with one row per receiver and columns for at
  least receiver id, position, received power (dBm), and path loss (dB)

#### Scenario: Missing pandas raises a clear error
- **WHEN** pandas is not installed and a caller calls `result.paths_dataframe()`
- **THEN** a clear error naming pandas SHALL be raised, while `import rftracekit` itself
  still succeeds

