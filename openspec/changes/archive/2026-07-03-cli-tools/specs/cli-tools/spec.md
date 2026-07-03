## ADDED Requirements

### Requirement: rftrace-cli runs a simulation end to end
`rftrace-cli` SHALL load a scene from a file, configure a simulation from command-line options, run it,
and write results to an output file. It SHALL support the point-receiver run, the coverage grid, and
the route modes; the scene format SHALL be detected from the input, and the output format SHALL be
inferred from the output file extension (`.json`, `.csv`, `.geojson`, `.gltf`, `.tif`). It SHALL accept
`--help` and `--version` and exit zero on success.

#### Scenario: Point run to JSON
- **WHEN** `rftrace-cli` is given a mesh scene, a transmitter and a receiver, and `--out result.json`
- **THEN** it SHALL run the simulation and write a JSON result file, exiting with status 0

#### Scenario: Coverage run to an inferred format
- **WHEN** `rftrace-cli` is run in coverage mode with grid options and `--out coverage.csv`
- **THEN** it SHALL write a coverage CSV (format inferred from the extension) with one value per cell

#### Scenario: Help and version
- **WHEN** `rftrace-cli --help` or `rftrace-cli --version` is invoked
- **THEN** it SHALL print usage / the library version and exit zero without running a simulation

### Requirement: Clear errors for bad input and unavailable formats
The CLIs SHALL fail with a clear, non-zero-exit error message (not a crash or silent success) for
missing/unreadable inputs, unparseable arguments, unknown formats, and formats that require an optional
feature not compiled in (e.g. GeoTIFF output without GDAL, Parquet without Arrow).

#### Scenario: Missing scene file
- **WHEN** `rftrace-cli` is given a scene path that does not exist
- **THEN** it SHALL print an error naming the problem and exit non-zero

#### Scenario: Unavailable output format
- **WHEN** an output format is requested whose optional feature is not compiled in
- **THEN** it SHALL print an error stating the feature is unavailable and exit non-zero, rather than
  writing a partial or empty file

### Requirement: scene-validator reports a summary and validates
`rftrace-scene-validator` SHALL load a scene and print a summary (triangle count, bounding box,
material count, transmitter and receiver counts). It SHALL detect and report problems (degenerate /
zero-area triangles, references to missing materials, an empty scene) and exit non-zero when the scene
is invalid, zero when it is valid.

#### Scenario: Valid scene
- **WHEN** the validator is run on a well-formed scene
- **THEN** it SHALL print the summary and exit zero

#### Scenario: Invalid scene
- **WHEN** the validator is run on a scene with degenerate geometry or no triangles
- **THEN** it SHALL report the specific problem and exit non-zero

### Requirement: result-converter converts between result formats
`rftrace-result-converter` SHALL read an `rftrace-cli` result file and write it in another supported
format, inferring the result kind (point vs coverage) and the output format from the file extensions.

#### Scenario: JSON result to CSV
- **WHEN** the converter reads a point-result JSON and is asked for a `.csv` output
- **THEN** it SHALL write the equivalent CSV and exit zero

#### Scenario: Unsupported conversion
- **WHEN** a conversion is requested that is not supported (incompatible kinds/formats)
- **THEN** it SHALL print a clear error and exit non-zero

### Requirement: CLI tools build behind a flag without new dependencies
The CLI tools SHALL build behind `RFTRACE_BUILD_CLI` (default ON), depending only on the core library
with no new third-party dependency (argument parsing is provided in-tree). With the flag off they
SHALL NOT be built, and the rest of the build and test suite SHALL be unaffected.

#### Scenario: Flag off leaves the build unchanged
- **WHEN** the project is built with `RFTRACE_BUILD_CLI=OFF`
- **THEN** the CLI executables SHALL NOT be built and the library and existing tests SHALL build and
  pass unchanged
