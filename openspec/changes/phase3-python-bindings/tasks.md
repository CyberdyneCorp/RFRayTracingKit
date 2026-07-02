## 1. Build System & Packaging (`python-packaging`)

- [x] 1.1 Implement `RFTRACE_ENABLE_PYTHON` in the main CMake: `find_package(pybind11 CONFIG)` first, fallback FetchContent pinned v2.13.6; find Python via the provided `Python3_EXECUTABLE`
- [x] 1.2 Add the `pybind11_add_module(rftracekit._native ...)` target under `bindings/python`, linking `rftrace::rftrace`, with `LIBRARY_OUTPUT_DIRECTORY` set to `bindings/python/rftracekit/`
- [x] 1.3 Verify the C++ core still builds and its 73 tests pass with `RFTRACE_ENABLE_PYTHON=OFF` (default) — no Python coupling in `src/`/`include/`
- [x] 1.4 Create the package layout (D2): `bindings/python/rftracekit/{__init__,scene,simulator,results,materials,antennas}.py`, `io/`, `viz/`
- [x] 1.5 Add `bindings/python/pyproject.toml` (scikit-build-core backend) for future pip install
- [x] 1.6 Add `just py-build` and `just py-test` recipes (CMake build + `PYTHONPATH=bindings/python` pytest)

## 2. Native Module & Core API (`python-bindings`)

- [x] 2.1 Bind `Vec3`, `Backend` (+`backend_from_string`/`to_string`), `Polarization`, `AntennaPattern` (Omnidirectional, `gain_towards`), `Material` (+`materials.preset`/`has_preset`)
- [x] 2.2 Bind `Scene` with snake_case methods: `add_material`, `material_index`, `add_mesh`, `add_transmitter`, `add_receiver`, `transmitters()`, `receivers()`, `load_mesh(path, material)`, `load_materials(path)`, `coordinate_system()`; map `SceneError` to a Python exception
- [x] 2.3 Bind `SimulationSettings` (backend, mode, `max_reflections`, `rays_per_transmitter`, `capture_radius`, `power_floor_dbm`, seed, coherent, `allow_backend_fallback`, `simulation_id`), `PropagationMode` (image/raylaunch), and `Simulator` (`run`, `run_coverage`)
- [x] 2.4 Bind result types `PathType`, `RFPath`, `ReceiverResult`, `TransmitterInfo`, `RFResult` (`receiver(id)`), `CoverageGrid` (origin, cell_size, cols, rows, height, `cell_center`, `cell_count`), `CoverageResult` (grid, `power_at`, `NoSignal`)
- [x] 2.5 Bind exporter entry points: JSON (`result_to_json_string`/`export_result_json`/`result_from_json_string`/`load_result_json`/coverage variants), CSV, GeoJSON, glTF
- [x] 2.6 Pure-Python ergonomic wrappers (`Scene`, `SimulationSettings`, `Simulator`, `Result`) with keyword args and snake_case; `import rftracekit as rf` exposes the documented top-level API
- [x] 2.7 Tests: native import, snake_case round-trip build a scene → `run` → `Result`, `run_coverage` → `CoverageResult`, `SceneError` surfaces, string enums (`backend='cpu'`, `mode='image'`)

## 3. NumPy & Pandas Interop (`python-numpy-interop`)

- [x] 3.1 Expose zero-copy/`py::array_t` buffers: `receiver_positions` float64[N,3], `received_power_dbm` float64[N], `path_loss_db` float64[N]
- [x] 3.2 Expose `coverage_array` float64[H,W] and path geometry as `points` float64[M,3] with int32 `offsets`[P+1]
- [x] 3.3 Guarantee array lifetime safety (keep-alive or owning copy); no dangling views after the result is destroyed
- [x] 3.4 Implement `Result.receivers_dataframe()` / `paths_dataframe()` with **lazy** pandas import and a clear error if pandas is missing (D5)
- [x] 3.5 Tests: array dtypes/shapes, values match scalar API, `NoSignal` sentinel present in `coverage_array`, offsets slice points into correct polylines, dataframe columns/rows, missing-pandas error path

## 4. Visualization (`python-visualization`)

- [x] 4.1 Implement `viz/pyvista.py::plot_paths(...)` (3D scene + colored path lines) with lazy `import pyvista`
- [x] 4.2 Implement `viz/plotly.py::plot_coverage(...)` (2D coverage heatmap) and interactive paths with lazy `import plotly`
- [x] 4.3 Wire `Result.plot_3d(engine='pyvista'|'plotly')` and `Result.plot_coverage(engine='plotly')` to delegate to viz helpers
- [x] 4.4 Ensure `import rftracekit` succeeds with neither pyvista nor plotly installed; viz calls raise a clear error naming the missing package (D4)
- [x] 4.5 Tests: bare import without optional deps, lazy-import error message, and (guarded/skipped when deps present) that plot functions return a figure/plotter object

## 5. Exports & Docs

- [x] 5.1 `Result.to_json(path)` / `to_csv(path)` / `to_geojson(path)` / `to_gltf(path)` delegate to the bound C++ exporters; tests assert files are produced and re-parse
- [x] 5.2 Add an `examples/python/` notebook or script (build scene → run → dataframe → plot → export)
- [x] 5.3 Update README/docs with the Python install/build, `PYTHONPATH` test workflow, and the §10 API surface
- [x] 5.4 `just py-build` and `just py-test` pass end-to-end; C++ `just ci` still green
