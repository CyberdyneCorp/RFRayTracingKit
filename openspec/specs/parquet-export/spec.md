# parquet-export Specification

## Purpose
TBD - created by archiving change core-io-formats. Update Purpose after archive.
## Requirements
### Requirement: Per-receiver Parquet export
The library SHALL, when built with Apache Arrow/Parquet, export the per-receiver result table with
columns `id`, `x`, `y`, `z`, `received_power_dbm`, `path_loss_db`, and `delay_spread_ns` to a
Parquet file.

#### Scenario: Receivers become Parquet rows
- **WHEN** a result with N receivers is exported to Parquet in a Parquet-enabled build
- **THEN** the file SHALL contain N rows and the columns `id`, `x`, `y`, `z`,
  `received_power_dbm`, `path_loss_db`, `delay_spread_ns`

#### Scenario: Parquet round-trips
- **WHEN** the exported Parquet file is reopened
- **THEN** its schema and row count SHALL match the exported table

### Requirement: Parquet graceful degradation without Arrow
The Parquet export API SHALL always be declared, and when the library is built without Arrow/Parquet
it SHALL throw a clear "built without Parquet" error and report unavailability via a
`parquetAvailable()`-style helper.

#### Scenario: Built without Parquet throws clearly
- **WHEN** Parquet export is called in a build compiled without Arrow/Parquet
- **THEN** it SHALL throw an error stating the library was built without Parquet, and
  `parquetAvailable()` SHALL return false

