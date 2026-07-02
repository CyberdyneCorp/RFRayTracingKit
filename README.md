# RFTraceKit

Modern **C++20** library for general **ray tracing** and **RF propagation simulation**
(4G/5G/6G and above). It takes a 3D scene (buildings, terrain, materials) plus
transmitters and receivers and computes propagation paths, received power, path loss,
delay, phase and multipath, and exports results for external visualization.

The engine is designed for multiple acceleration backends (CPU, Metal, CUDA/OptiX,
OpenCL) behind a shared, backend-agnostic RF-physics core. This repository implements
**Phase 1 (CPU reference)** and **Phase 2 (RF multipath + coverage)** on the CPU backend
— the correctness baseline all future GPU backends are validated against.

> Development is spec-driven with [OpenSpec](https://openspec.dev). The living specs are
> in `openspec/`; the current change is `openspec/changes/phase1-cpu-prototype/`.

## Phase 1 capabilities

- Backend-agnostic **scene model** — meshes, materials (with built-in presets),
  transmitters, receivers, antenna patterns, Z-up coordinate system.
- **Core geometry** — Eigen-based `Vec3`/`Ray`/`Triangle`/`AABB`, Möller–Trumbore
  intersection, and a NanoRT-style **BVH** with closest-hit and occlusion queries.
- **Scene import** — triangle meshes (glTF/OBJ via Assimp, normalized to Z-up) and
  material definitions (JSON).
- **RF propagation** — free-space path loss, Fresnel reflection, penetration loss,
  phase, delay, antenna gain, and coherent/incoherent power aggregation.
- **Ray simulation** — `Backend` abstraction + CPU backend; point-receiver mode with
  line-of-sight and specular reflections (image method, up to `maxReflections`).
- **Results export** — per-receiver aggregation (power, path loss, delay spread) and
  **JSON** / **CSV** export.

## Phase 2 capabilities

- **Stochastic ray launch** — Monte-Carlo ray launching (Fibonacci-sphere sampling) with
  multi-bounce specular tracing and a receiver capture sphere; deduplicated and seed-
  reproducible. Selected via `SimulationSettings.mode = PropagationMode::RayLaunch`. The
  deterministic image method remains the correctness oracle.
- **Multi-bounce** — first-class `maxReflections > 1` in both propagation modes.
- **Coverage grid** — `Simulator::runCoverage(scene, grid)` evaluates received power over
  a georeferenced 2D grid, with a no-signal sentinel.
- **GeoJSON export** — receivers (points), ray paths (lines), coverage cells (polygons).
- **glTF export** — debug ray-path lines colored by power + receiver points.

Ray-launch aggregate power agrees with the image method within **≤1 dB** on the golden
single-wall scene at a pinned budget (600k rays, 3 m capture radius).

## Phase 3 capabilities (Python bindings)

A pybind11 module (`rftracekit._native`) plus a pure-Python `rftracekit` package expose the
engine to Python. The C++ core stays Python-free; all coupling lives under `bindings/python/`.

```python
import rftracekit as rf

scene = rf.Scene()
scene.add_transmitter(id="tower_1", position=[120, 80, 35], frequency_hz=3.5e9, power_dbm=43)
scene.add_receiver(id="rx_001", position=[300, 180, 1.5])

result = rf.Simulator(rf.SimulationSettings(mode="raylaunch", max_reflections=3)).run(scene)

df = result.receivers_dataframe()          # pandas (optional)
pos = result.receiver_positions            # numpy float64[N,3]
result.to_geojson("paths.geojson", kind="paths")
result.to_gltf("debug_rays.gltf")
```

- **NumPy interop**: `receiver_positions` `[N,3]`, `received_power_dbm`/`path_loss_db` `[N]`,
  `path_points` `[M,3]` + `path_offsets` `[P+1]`, coverage `coverage_array` `[H,W]`.
- **pandas** (optional): `receivers_dataframe()`, `paths_dataframe()`.
- **Visualization** (optional, lazy): `rftracekit.viz` + `Result.plot_3d(engine=...)` and
  `CoverageResult.plot_coverage(engine="plotly")`; the package imports fine without
  pyvista/plotly/pandas installed.
- **Build/test**: `just py-build` and `just py-test` (needs a `python3` with `pybind11` +
  `numpy`). The extension builds only when `-DRFTRACE_ENABLE_PYTHON=ON`.

Out of scope until later phases: GPU backends, Python bindings, terrain/GeoTIFF,
diffraction, atmospheric/vegetation attenuation, MIMO, route simulation, CZML/3D-Tiles.
See `openspec/project.md` for the full roadmap.

## Building

Requires CMake ≥ 3.25, a C++20 compiler, and Eigen, Assimp and nlohmann/json.
GoogleTest is fetched automatically (pinned) unless `-DRFTRACE_USE_SYSTEM_GTEST=ON`.

Dependencies are resolved via **vcpkg** (set `VCPKG_ROOT`) or any system package
manager through CMake config-mode `find_package` (e.g. Homebrew:
`brew install eigen assimp nlohmann-json`).

### With `just` (recommended)

```bash
just build      # configure + compile library, tests, examples
just test       # build + run the test suite
just ci         # clang tests + gcc + asan + openspec validate + examples
just example simple_los
just --list     # all recipes
```

### With CMake directly

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Optional backend flags: `-DRFTRACE_ENABLE_METAL/CUDA/OPENCL/EMBREE=ON`,
`-DRFTRACE_ENABLE_PYTHON=ON` (no-ops until their phases land).

## Public API example

```cpp
#include "rftrace/rftrace.hpp"
using namespace rftrace;

Scene scene;
scene.addMaterial(materials::preset("concrete"));
scene.loadMesh("city.glb", "concrete");        // glTF/OBJ, normalized to Z-up

Transmitter tx;
tx.id = "tower_1";
tx.position = {120.0, 80.0, 35.0};             // Z is height
tx.frequencyHz = 3.5e9;
tx.powerDbm = 43.0;
scene.addTransmitter(tx);

scene.addReceiver(Receiver{.id = "rx_001", .position = {300.0, 180.0, 1.5}});

SimulationSettings settings;
settings.maxReflections = 3;

RFResult result = Simulator(settings).run(scene);
io::exportResultJson(result, "paths.json");
io::exportReceiversCsv(result, "receivers.csv");
```

See `examples/simple_los` (line-of-sight link budget), `examples/city_reflection`
(LOS + specular reflection off a wall), and `examples/coverage_grid` (coverage-grid mode
with CSV/JSON/GeoJSON export).

## Coordinate convention

The core uses a right-handed **Z-up** frame (Z = height/elevation), matching GIS/Cesium
and the spec's coordinate examples. glTF/OBJ meshes (Y-up) are rotated to Z-up on import.

## Performance

CPU BVH closest-hit throughput is ~0.5 Mrays/s single-threaded (Release) over a
50k-triangle scene on an Apple-silicon dev machine — within the spec's 100k–1M rays/s
Phase 1 target. Re-measure per machine; multi-threading and GPU backends come later.

## License

TBD.
