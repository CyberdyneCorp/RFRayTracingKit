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

## Phase 4 capabilities (Metal GPU backend)

A native Apple **Metal** backend implements the `IBackend` traversal contract with hardware
ray tracing (`MTLAccelerationStructure` + a runtime-compiled `metal_raytracing` compute
kernel). RF physics stays backend-agnostic — Metal only accelerates traversal.

- **Batched query API** on `IBackend` (`closestHitBatch` / `occludedBatch`, default CPU-loop
  impl) — where GPU acceleration pays off; the Metal backend overrides them with one dispatch.
- **CPU-vs-Metal parity** is validated on the golden scenes + random geometry (~40k ray
  comparisons; float-vs-double tolerance `|Δt| ≤ max(1e-2 m, 1e-4·|t|)`, matching triangle
  indices for well-separated geometry). Tests `GTEST_SKIP` when no GPU is present.
- **Selection / fallback**: `Backend::Metal` is used when built with `-DRFTRACE_ENABLE_METAL=ON`
  and a ray-tracing-capable device exists; otherwise the engine falls back to CPU.
- Build/run: `just metal` (configures `build-metal` with the flag and runs its ctest). Metal
  is not part of the default `ci` recipe.

> Note: the Phase 1 image-method simulator still issues per-ray queries, so selecting Metal
> as the *simulator* backend is correct but not yet faster than CPU; the batched API is the
> foundation for a future batched simulator path. Today Metal is a validated, batch-capable
> traversal backend and the reference for the CUDA/OpenCL backends to come.

## OpenCL GPU backend

A portable **OpenCL** backend implements the same `IBackend` traversal contract. OpenCL 1.2
has no hardware ray tracing, so the backend builds a custom **flat BVH** from the scene
triangles (exposed additively from the CPU `BVH`), uploads it as float32 device buffers, and
traverses it with a runtime-compiled OpenCL C kernel that uses an explicit fixed-size stack
(no recursion) and a Möller–Trumbore triangle test. A closest-hit kernel keeps the nearest
hit; an any-hit kernel early-exits at the first occluder. RF physics stays backend-agnostic —
OpenCL only accelerates traversal, and all public types remain double precision (values
convert to float only inside the device buffers).

- **Batched dispatch**: `closestHitBatch` / `occludedBatch` upload the rays once and service
  the whole batch in a single `clEnqueueNDRangeKernel`; single-ray queries run as a batch of 1.
- **CPU-vs-OpenCL parity** is validated on the golden scenes + hundreds of random triangles
  (thousands of rays per scene; float-vs-double tolerance `|Δt| ≤ max(1e-2 m, 1e-4·|t|)`,
  matching triangle indices for well-separated geometry), plus a determinism check. Tests
  `GTEST_SKIP` when no OpenCL device is present.
- **Selection / fallback**: `Backend::OpenCL` is used when built with `-DRFTRACE_ENABLE_OPENCL=ON`
  (which requires `find_package(OpenCL)` to succeed, defining `RFTRACE_HAVE_OPENCL`) and a GPU
  device exists at runtime; otherwise the engine falls back to CPU.
- Build/run: `just opencl` (configures `build-opencl` with the flag and runs its ctest). OpenCL
  is not part of the default `ci` recipe.

> Verified on Apple OpenCL 1.2 (Apple M2 Max) and portable to other OpenCL 1.2+ GPUs. Apple's
> OpenCL is deprecated but functional; the build defines `CL_SILENCE_DEPRECATION` and
> `CL_TARGET_OPENCL_VERSION=120` to pin/quiet the API.

## CUDA / OptiX GPU backend

A native **NVIDIA CUDA/OptiX** backend implements the same `IBackend` traversal contract with
hardware ray tracing (RT cores). `build()` uploads float32 vertex/index buffers and builds an
OptiX geometry acceleration structure (GAS) with compaction; a single `optixLaunch` over device
programs (raygen + closesthit + miss, compiled to PTX and loaded at runtime via
`optixModuleCreate`) intersects each ray batch. The primitive index equals the `Triangle`
index, so hits map straight back. RF physics stays backend-agnostic — CUDA only accelerates
traversal, and all public types remain double precision (values convert to float only inside
the device buffers).

- **Batched dispatch**: `closestHitBatch` / `occludedBatch` upload the rays once and service the
  whole batch in one `optixLaunch` (closest-hit uses `OPTIX_RAY_FLAG_DISABLE_ANYHIT`; occlusion
  adds `OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT`); single-ray queries run as a batch of 1.
- **CPU-vs-CUDA parity** tests mirror the Metal/OpenCL suites (float-vs-double tolerance
  `|Δt| ≤ max(1e-2 m, 1e-4·|t|)`, matching triangle indices for well-separated geometry, plus a
  determinism check). Tests `GTEST_SKIP` when no CUDA device is present.
- **Selection / fallback**: `Backend::CUDA` is used when built with `-DRFTRACE_ENABLE_CUDA=ON`
  (which requires `find_package(CUDAToolkit)` and an OptiX SDK, defining `RFTRACE_HAVE_CUDA`) and
  a device exists at runtime; otherwise the engine falls back to CPU.
- Build/run: `just cuda` (configures `build-cuda` with the flag and runs its ctest). Set
  `-DOptiX_INSTALL_DIR=/path/to/OptiX-SDK` (the directory containing `include/optix.h`); tune the
  device PTX target with `-DCMAKE_CUDA_ARCHITECTURES`. CUDA is not part of the default `ci`
  recipe. On a host without a CUDA Toolkit / OptiX SDK the configure step fails fast with a clear
  message.

> **UNVERIFIED on non-NVIDIA hosts.** This backend is authored to the CUDA runtime + OptiX 7.7/8
> host API and mirrors the working Metal backend, but it has **not been compiled or run** on the
> Apple development host (no `nvcc` / OptiX). Enabling `-DRFTRACE_ENABLE_CUDA=ON` there fails at
> configure time by design. It must be validated on NVIDIA + OptiX hardware; the default build
> (flag off) never compiles any CUDA source.

## Phase 7 capabilities (advanced RF)

Physically richer propagation and system-level metrics, all on the CPU backend and validated
against published-model reference values. Every feature is **additive and default-off** — with
default settings, results are identical to Phase 1/2 (enforced by a regression test).

- **Diffraction** — ITU-R P.526 single knife-edge (J(0) ≈ 6 dB); diffracted paths over the
  dominant blocking edge when `settings.enableDiffraction`.
- **Atmospheric attenuation** — rain (ITU-R P.838-3, `γ=k·Rᵃ`) and gaseous (P.676) specific
  attenuation per path length (`enableRain` / `rainRateMmPerHr`, `enableGaseousAttenuation`).
- **Vegetation attenuation** — Weissberger/P.833 foliage loss over the in-foliage path depth
  through vegetation-material geometry (`enableVegetation`).
- **Antenna arrays** — ULA/UPA geometry, array factor, beam steering; a steered array's gain
  replaces the single-element gain in the per-path budget.
- **MIMO** — channel matrix H from per-path gains + array responses; narrowband equal-power
  capacity `log2 det(I + (SNR/M)·H·Hᴴ)` and per-stream SINR; JSON export.
- **Cell planning / SINR** — serving-cell selection and SINR = S/(I+N) with a physically
  derived noise floor `N = kTB + NF` (`enableSinr`, `noiseBandwidthHz`, `noiseFigureDb`); SINR
  coverage maps.
- **Route simulation** — moving receiver sampled along waypoints → ordered drive-test series
  with CSV/JSON export (`Simulator::runRoute`).

See `examples/advanced_rf` (diffraction + two-cell SINR coverage + drive-test route). The
Phase 7 path-loss toggles are drivable from Python via `SimulationSettings` (route/MIMO/array
Python APIs are a documented follow-up).

Out of scope until later phases: terrain/GeoTIFF, full UTD diffraction, CZML/3D-Tiles route
animation. See `openspec/project.md` for the full roadmap.

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
