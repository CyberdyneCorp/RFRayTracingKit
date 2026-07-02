## Context

The C++20 core (namespace `rftrace`) is complete through Phase 2 and exposes a clean public
API in `include/rftrace/*.hpp`: `Scene`, `Material`/`materials::preset`, `AntennaPattern`,
`Backend`, `SimulationSettings`/`Simulator`, `RFResult`/`CoverageResult`, and the JSON/CSV/
GeoJSON/glTF exporters. Phase 3 wraps this for Python users without disturbing the core. The
overriding constraint: **the C++ core must not depend on Python**; every Python touchpoint
lives under `bindings/python/`, and the core plus its 73 C++ tests must keep building and
passing with `RFTRACE_ENABLE_PYTHON=OFF` (the default).

## Goals / Non-Goals

**Goals:**
- One pybind11 extension `rftracekit._native` that faithfully exposes the public C++ API.
- An ergonomic pure-Python `rftracekit` package: snake_case names, numpy/pandas-native, with
  a `Result` object offering dataframes, arrays, exports, and plots.
- Zero-copy numpy views of large buffers; lazily-imported pandas dataframes.
- Optional pyvista/plotly visualization that never breaks a bare import.
- A reproducible CMake build (like the existing GoogleTest FetchContent style) plus a
  scikit-build-core `pyproject.toml` for future pip installs.

**Non-Goals:**
- GPU backends, terrain/GeoTIFF, diffraction, MIMO/beamforming.
- Publishing wheels to PyPI or building wheels in CI (pyproject is scaffolding for later).
- Any change to C++ physics, result semantics, or exporter output.
- Making numpy/pandas/pyvista/plotly hard runtime requirements of a bare `import rftracekit`.

## Decisions

### D-A. Thin native module + pure-Python ergonomic layer
`_native` is a near-1:1 pybind11 wrapping of the C++ API; taste (defaults, keyword args,
snake_case, dataframes, plots) lives in pure Python. This keeps the compiled surface small
and testable and lets the ergonomic API evolve without recompiling. **Alternative:** put all
ergonomics in C++/pybind — rejected; slower to iterate and harder to read.

### D-B. snake_case at the binding boundary
pybind `def`/`def_property` names and wrapper methods are snake_case (`add_transmitter`,
`received_power_dbm`), even though C++ is camelCase, matching Python conventions and the
source spec §10. **Alternative:** expose camelCase — rejected; un-Pythonic.

### D-C. Core stays Python-free; extension isolated under bindings/
No `#include <pybind11>` or Python symbols anywhere in `src/`/`include/`. The extension is a
separate CMake target under `bindings/python/` guarded by `RFTRACE_ENABLE_PYTHON`, linking
`rftrace::rftrace`. **Alternative:** conditional Python hooks in the core — rejected;
violates the architectural principle and the phase rules.

## Risks / Trade-offs

- **Zero-copy lifetime** — numpy views must not outlive the C++ buffer. Mitigation: use
  pybind `py::keep_alive` / return arrays that own a copy where lifetime can't be guaranteed;
  test that arrays survive result destruction (copy) or are documented as views.
- **Optional-dep import failures** — pandas/pyvista/plotly may be missing. Mitigation: import
  them lazily inside the functions that need them and raise a clear, actionable error; a bare
  `import rftracekit` must succeed with none of them installed (tested).
- **Build reproducibility** — pybind11 may be absent. Mitigation: `find_package(pybind11
  CONFIG)` first (its cmake dir is on `CMAKE_PREFIX_PATH`), fallback to FetchContent pinned
  to v2.13.6, mirroring the GoogleTest pattern.
- **NumPy 1-vs-2 warnings** from pandas import are harmless and ignored.

## Migration Plan

Purely additive. `RFTRACE_ENABLE_PYTHON` defaults OFF, so existing C++ builds/tests are
unaffected. When ON, the extension builds into `bindings/python/rftracekit/` and Python
tests run with `PYTHONPATH=bindings/python`. No existing files change behavior; the only
edits outside `bindings/` are the CMake `RFTRACE_ENABLE_PYTHON` implementation and the new
`justfile` recipes.

## Resolved Decisions

These are final for Phase 3 (no open questions).

### D1. Extension module
One pybind11 module named `rftracekit._native`, built by the **main** CMake when
`RFTRACE_ENABLE_PYTHON=ON`, linking `rftrace::rftrace`. pybind11 is acquired via
`find_package(pybind11 CONFIG)` (its cmake dir is on `CMAKE_PREFIX_PATH`), with a fallback
to FetchContent pinned at **v2.13.6**. Python is found via the provided `Python3_EXECUTABLE`.

### D2. Package layout (pure-Python wrappers around `_native`)
```
bindings/python/rftracekit/__init__.py
bindings/python/rftracekit/scene.py        (ergonomic Scene helpers)
bindings/python/rftracekit/simulator.py
bindings/python/rftracekit/results.py       (Result: dataframes, arrays, exports, plot_*)
bindings/python/rftracekit/materials.py
bindings/python/rftracekit/antennas.py
bindings/python/rftracekit/io/__init__.py
bindings/python/rftracekit/viz/__init__.py
bindings/python/rftracekit/viz/pyvista.py
bindings/python/rftracekit/viz/plotly.py
```
The compiled `_native.*.so` is built **into** `bindings/python/rftracekit/` (via the
pybind11 target's `LIBRARY_OUTPUT_DIRECTORY`), so tests run with
`PYTHONPATH=bindings/python`. A `pyproject.toml` under `bindings/python`
(scikit-build-core backend) enables future `pip install`, but Phase 3 tests use the CMake
build + `PYTHONPATH`, **not** pip.

### D3. Zero-copy / numpy
Expose large result buffers as numpy arrays via `py::array_t`/buffer protocol where
practical: `receiver_positions` float64[N,3], `received_power_dbm` float64[N],
`path_loss_db` float64[N], `coverage_array` float64[H,W]; path geometry as `points`
float64[M,3] with int32 `offsets`[P+1] (CSR-style polyline slicing). Small scalar fields are
copied.

### D4. Visualization
`pyvista`/`plotly` are **optional** and imported lazily inside functions; the package must
import fine without them. viz helpers `plot_paths()` / `plot_coverage()`; `Result` has
`plot_3d(engine='pyvista'|'plotly')` and `plot_coverage(engine='plotly')` delegating to viz.

### D5. pandas optional
pandas is optional at import time; `results.receivers_dataframe()` / `paths_dataframe()`
import pandas lazily and raise a clear error if it is missing.
