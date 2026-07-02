## ADDED Requirements

### Requirement: CMake-driven extension build
The main CMake build SHALL, when `RFTRACE_ENABLE_PYTHON=ON`, build the `rftracekit._native`
extension, acquiring pybind11 via `find_package(pybind11 CONFIG)` with a fallback to
FetchContent pinned at v2.13.6, and locating Python via the provided `Python3_EXECUTABLE`.
When `RFTRACE_ENABLE_PYTHON=OFF` (the default) no Python or pybind11 dependency SHALL be
required.

#### Scenario: Extension builds when enabled
- **WHEN** CMake is configured with `RFTRACE_ENABLE_PYTHON=ON` and a valid
  `Python3_EXECUTABLE` and pybind11 available on `CMAKE_PREFIX_PATH`
- **THEN** the build SHALL produce the `rftracekit._native` extension module

#### Scenario: Python is not required when disabled
- **WHEN** CMake is configured with `RFTRACE_ENABLE_PYTHON=OFF`
- **THEN** configuration and build SHALL succeed with no pybind11 or Python requirement

### Requirement: Package tree build output and test layout
The compiled `_native` extension SHALL be emitted into `bindings/python/rftracekit/` (via the
target's library output directory) so that the package is importable and testable with
`PYTHONPATH=bindings/python`, without requiring a pip install.

#### Scenario: Tests run against the built package via PYTHONPATH
- **WHEN** the extension is built and pytest is run with
  `PYTHONPATH=bindings/python`
- **THEN** `import rftracekit` SHALL resolve the pure-Python package alongside the compiled
  `_native` module and the Python test suite SHALL run

### Requirement: pyproject for future pip installs
A `pyproject.toml` under `bindings/python` SHALL declare a scikit-build-core build backend so
the package can later be `pip install`ed, while Phase 3 tests continue to use the CMake build
plus `PYTHONPATH`.

#### Scenario: pyproject declares the build backend
- **WHEN** the `bindings/python/pyproject.toml` is inspected
- **THEN** it SHALL declare `scikit-build-core` as the build backend and name the
  distribution `rftracekit`

### Requirement: Task-runner recipes
The project justfile SHALL provide `py-build` and `py-test` recipes that configure/build the
extension and run the Python test suite with the correct `PYTHONPATH`.

#### Scenario: py-test runs the Python suite
- **WHEN** a developer runs `just py-test`
- **THEN** the recipe SHALL build the extension and run pytest against the package with
  `PYTHONPATH` set to the package directory
