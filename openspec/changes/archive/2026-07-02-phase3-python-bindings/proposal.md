## Why

Phases 1 and 2 delivered a complete, tested CPU RF-propagation core in C++20:
deterministic image-method paths, stochastic ray launching, multi-bounce reflections,
coverage grids, and JSON/CSV/GeoJSON/glTF exporters. But the intended users — RF planners,
researchers, data scientists — work in Python notebooks, not C++. Today the only way to
drive the engine is to write and compile C++. Phase 3 closes that gap by exposing the
existing C++ API to Python through a pybind11 native module, wrapping it in an ergonomic,
snake_case, numpy/pandas-native package, and adding optional 3D/2D visualization — turning
the reference core into a usable, notebook-friendly tool. Crucially, the C++ core stays
Python-free: all coupling lives under `bindings/python/`, so the engine still builds and
its 73 tests still pass with `RFTRACE_ENABLE_PYTHON=OFF`.

## What Changes

- Add a **pybind11 native extension** `rftracekit._native`, built by the main CMake when
  `RFTRACE_ENABLE_PYTHON=ON`, linking `rftrace::rftrace`. It exposes Scene, Material/antenna
  presets, `SimulationSettings`, `Simulator`, `RFResult`/`CoverageResult`, and the exporter
  entry points, using snake_case Python names even though the C++ is camelCase.
- Add a **pure-Python package** `rftracekit` (thin, ergonomic wrappers around `_native`):
  `Scene`, `SimulationSettings`, `Simulator`, and a rich `Result` wrapper with dataframes,
  numpy arrays, exports, and plotting.
- Add **zero-copy numpy interop**: expose large result buffers (receiver positions,
  received power, path loss, coverage array, path geometry as points+offsets) as numpy
  arrays via the buffer protocol / `py::array_t`, and **pandas dataframes** (lazily
  imported) for receivers and paths.
- Add **optional visualization**: `pyvista` (3D scene/paths) and `plotly` (2D coverage /
  interactive paths) helpers, imported lazily so the package imports fine without them.
- Add **packaging**: build the compiled `_native.*.so` into
  `bindings/python/rftracekit/` (so tests run via `PYTHONPATH`), and a `pyproject.toml`
  (scikit-build-core backend) under `bindings/python` for future `pip install`.
- Add **Python tests** (pytest) covering the native module, snake_case API, numpy
  zero-copy/shapes, pandas dataframes, exporter round-trips, and graceful degradation when
  optional deps are absent; plus `just py-build` / `just py-test` recipes.

## Capabilities

### New Capabilities
- `python-bindings`: the `rftracekit._native` pybind11 module and the pure-Python
  Scene/Simulator/Result API (snake_case), including exporter entry points.
- `python-numpy-interop`: zero-copy numpy views of result buffers and lazily-imported
  pandas dataframes for receivers and paths.
- `python-visualization`: optional, lazily-imported pyvista/plotly helpers
  (`plot_paths`, `plot_coverage`) and `Result.plot_3d()`/`plot_coverage()`.
- `python-packaging`: CMake-driven extension build into the package tree, `PYTHONPATH`-based
  test layout, and a scikit-build-core `pyproject.toml` for future pip installs.

## Impact

- **Code:** new `bindings/python/` tree (native `src/*.cpp` for the pybind11 module,
  `rftracekit/` Python package, `pyproject.toml`, `tests/`); CMake gains a real
  `RFTRACE_ENABLE_PYTHON` implementation (pybind11 via `find_package`, fallback
  FetchContent pinned v2.13.6). `justfile` gains `py-build`/`py-test`.
- **C++ core:** unchanged behavior; the core must not link or include Python. Only additive
  binding glue lives under `bindings/python/`.
- **Dependencies:** pybind11 (build-time, for the extension); numpy required by the numpy
  interop; pandas, pyvista, plotly all **optional** at import time.
- **Out of scope (later phases):** GPU backends (Metal/CUDA/OpenCL), terrain/GeoTIFF,
  diffraction, MIMO/beamforming, published PyPI wheels/CI wheel-building, and any change to
  the C++ physics or result semantics.
