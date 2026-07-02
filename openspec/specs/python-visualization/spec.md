# python-visualization Specification

## Purpose
TBD - created by archiving change phase3-python-bindings. Update Purpose after archive.
## Requirements
### Requirement: Optional visualization backends
The Python package SHALL treat `pyvista` and `plotly` as optional dependencies, importing
them lazily inside the visualization functions so that `import rftracekit` succeeds when
neither is installed.

#### Scenario: Package imports without visualization dependencies
- **WHEN** neither pyvista nor plotly is installed and a caller runs `import rftracekit`
- **THEN** the import SHALL succeed and the non-visualization API SHALL remain fully usable

#### Scenario: Missing visualization dependency raises a clear error
- **WHEN** a caller invokes a visualization function whose backend package is not installed
- **THEN** a clear error naming the missing package (pyvista or plotly) SHALL be raised only
  at call time, not at import time

### Requirement: Path and coverage visualization helpers
The package SHALL provide `plot_paths()` (3D scene and ray paths) and `plot_coverage()` (2D
coverage) visualization helpers under `rftracekit.viz`, backed by pyvista and/or plotly.

#### Scenario: Plot paths returns a renderable object
- **WHEN** pyvista is installed and a caller calls `rftracekit.viz.plot_paths(result)`
- **THEN** a pyvista plotter/renderable object depicting the scene and ray paths SHALL be
  returned

#### Scenario: Plot coverage returns a figure
- **WHEN** plotly is installed and a caller calls `rftracekit.viz.plot_coverage(coverage)`
- **THEN** a plotly figure depicting the coverage grid as a 2D heatmap SHALL be returned

### Requirement: Result plotting convenience methods
The Python `Result` SHALL provide `plot_3d(engine='pyvista'|'plotly')`, and the Python
`CoverageResult` SHALL provide `plot_coverage(engine='plotly')`; both delegate to the
`rftracekit.viz` helpers for the chosen engine. (Path/receiver geometry lives on `Result`
and grid coverage lives on `CoverageResult`, mirroring the C++ `RFResult`/`CoverageResult`
split, so each plotting method sits on the type that carries its data.)

#### Scenario: Result delegates 3D plotting to the chosen engine
- **WHEN** a caller calls `result.plot_3d(engine='pyvista')` with pyvista installed
- **THEN** the call SHALL delegate to the pyvista path helper and return its renderable
  object

#### Scenario: CoverageResult delegates coverage plotting to the chosen engine
- **WHEN** a caller calls `coverage.plot_coverage(engine='plotly')` with plotly installed
- **THEN** the call SHALL delegate to the plotly coverage helper and return its figure

