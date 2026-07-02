## ADDED Requirements

### Requirement: Native pybind11 extension module
The library SHALL provide a pybind11 extension module named `rftracekit._native`, built by
the main CMake build when `RFTRACE_ENABLE_PYTHON=ON` and linked against `rftrace::rftrace`,
that exposes the public C++ API (scene, materials, antennas, backend, simulator, results,
coverage, exporters) to Python. The C++ core SHALL NOT depend on Python: no Python or
pybind11 symbols appear in `src/` or `include/`.

#### Scenario: Native module imports and reports the core is available
- **WHEN** Python runs `import rftracekit._native` after a build with `RFTRACE_ENABLE_PYTHON=ON`
- **THEN** the import SHALL succeed and expose the Scene, Simulator, and result types
  bound from the C++ core

#### Scenario: Core builds and tests pass without Python
- **WHEN** the C++ project is configured and built with `RFTRACE_ENABLE_PYTHON=OFF`
- **THEN** the library and its existing C++ test suite SHALL build and pass unchanged, with
  no link or compile dependency on Python

### Requirement: Ergonomic snake_case Python API
The Python package `rftracekit` SHALL expose an ergonomic, snake_case API wrapping the
native module, such that `import rftracekit as rf` gives access to `Scene`,
`SimulationSettings`, `Simulator`, materials, and antennas, with Python-conventional
method and property names even though the C++ API is camelCase.

#### Scenario: Build a scene with snake_case methods
- **WHEN** a caller does `scene = rf.Scene()` then
  `scene.add_transmitter(id=..., position=[x,y,z], frequency_hz=..., power_dbm=...)` and
  `scene.add_receiver(id=..., position=[x,y,z])`
- **THEN** the transmitter and receiver SHALL be registered in the underlying C++ scene and
  visible via `scene.transmitters()` / `scene.receivers()`

#### Scenario: Settings accept string enums
- **WHEN** a caller constructs `rf.SimulationSettings(backend='cpu', mode='image',
  max_reflections=2, seed=1)`
- **THEN** the string values SHALL map to the corresponding C++ `Backend` and
  `PropagationMode` enums

### Requirement: Simulation from Python returns a Result
The Python `Simulator` SHALL run a scene and return a `Result` object, and SHALL run a
coverage grid and return a coverage result, delegating to the C++ `Simulator::run` and
`Simulator::runCoverage`.

#### Scenario: Run a scene and receive a Result
- **WHEN** a caller does `result = rf.Simulator(settings).run(scene)`
- **THEN** `result` SHALL expose per-receiver aggregated results equivalent to the C++
  `RFResult`, including lookup of a receiver by id

#### Scenario: Run a coverage grid
- **WHEN** a caller does `cov = rf.Simulator(settings).run_coverage(scene, grid)`
- **THEN** `cov` SHALL expose the coverage grid metadata and per-cell power equivalent to
  the C++ `CoverageResult`

### Requirement: Scene errors surface as Python exceptions
Invalid scene operations that raise `SceneError` in C++ SHALL raise a corresponding Python
exception rather than crashing the interpreter.

#### Scenario: Duplicate id raises in Python
- **WHEN** a caller adds two transmitters with the same id
- **THEN** a Python exception carrying the C++ `SceneError` message SHALL be raised

### Requirement: Result export entry points
The Python `Result` SHALL expose export methods (`to_json`, `to_csv`, `to_geojson`,
`to_gltf`) that delegate to the bound C++ exporters and write the corresponding file.

#### Scenario: Export a result to JSON
- **WHEN** a caller does `result.to_json(path)`
- **THEN** a JSON file SHALL be written at `path` and SHALL re-parse into an equivalent
  result via the JSON import entry point
