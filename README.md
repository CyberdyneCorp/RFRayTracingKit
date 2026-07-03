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

> Note: the image-method simulator still issues per-ray queries, so selecting a GPU backend as
> the *simulator* backend is correct but not yet faster than CPU. The batched query API — now
> including the zero-allocation caller-owned-output forms (`closestHitBatchInto` /
> `occludedBatchInto`) — is the foundation for a batched simulator path (the next roadmap step
> for the GPU backends). Today Metal, CUDA (verified on an RTX 5060), and OpenCL are validated,
> batch-capable traversal backends.

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
- **Benchmark**: `rftrace_cuda_benchmark [triangles] [rays] [seed]` (example target) times CPU vs
  CUDA for `build()`, `closestHitBatch()`, and `occludedBatch()` on a procedural city, with a
  correctness cross-check. On an RTX 5060 (OptiX 9.0.0) over 1M rays: a 1M-triangle scene gives
  ≈**475×** closest-hit and ≈**150×** occlusion speedup (100% hit/miss agreement), and the closest-hit
  speedup grows with scene size as the CPU BVH slows. It runs CPU-only when no GPU is present.
- **Optimised query path**: the per-query buffers are pooled across dispatches (device buffers plus
  page-locked *pinned* host staging), grown on demand. Rays are converted straight into pinned
  memory (no zero-initialised staging vector) and streamed with async H2D/D2H copies, so the only
  host work per launch is the unavoidable double→float conversion. Profiled on 1M rays (set
  `RFTRACE_CUDA_PROFILE=1`): the device dispatch is ~7 ms (~140 Mray/s) — convert ≈4.5 ms, launch
  ≈0.8 ms (the RT cores trace 1M rays in under a millisecond), H2D/D2H the rest. Occlusion reaches
  ~120 Mray/s end-to-end; `closestHitBatch` is then bounded by allocating its returned
  `std::vector<Hit>` (~40 MB for 1M rays — an API cost borne equally by the CPU backend), not the
  device path.
- **Caller-owned output API**: `IBackend::closestHitBatchInto(rays, std::span<Hit>)` and
  `occludedBatchInto(rays, std::span<char>)` write one result per ray into a buffer the caller owns
  and can reuse across batches, allocating nothing for the output. The vector-returning
  `closestHitBatch`/`occludedBatch` are thin wrappers over these. Reusing one buffer removes the
  per-call ~40 MB result allocation, so a hot GPU loop runs **~1.9× faster** closest-hit (≈48 →
  ≈91 Mray/s, 1M rays on an RTX 5060), approaching the device-dispatch ceiling. The `Into` forms
  overwrite every slot (misses included), so a reused/dirty buffer stays correct.

> **Verified on NVIDIA hardware.** The backend and its parity suite have been compiled and run on
> an NVIDIA GeForce RTX 5060 (Blackwell, `sm_120`) — CUDA Toolkit 12.0, driver 580.95.05, **OptiX
> SDK 9.0.0** — where all CPU-vs-CUDA parity tests pass. The default build (flag off) never
> compiles any CUDA source.
>
> **OptiX SDK ↔ driver ABI:** `optixInit()` fails with `OPTIX_ERROR_UNSUPPORTED_ABI_VERSION` when
> the SDK's `OPTIX_ABI_VERSION` exceeds what the installed driver's `libnvoptix` implements (query
> it: `strings libnvoptix.so.* | grep 'ABI Version'`). The backend uses only OptiX ≥ 7.7 API, so
> pick any SDK whose ABI the driver supports — e.g. driver 580.95.05 ships OptiX 9.0.2 (ABI 110),
> so OptiX SDK 8.x–9.0.x work but 9.1 (ABI 118) does not. Availability is gated at runtime, so a
> too-new SDK simply falls back to CPU rather than crashing.

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
Phase 7 path-loss toggles are drivable from Python via `SimulationSettings`.

## Geospatial IO & Python surface

**Core import:** glTF/OBJ (Assimp), **GeoJSON**, **CityJSON**, **OSM** (Overpass JSON, `.osm`
XML — always on; `.osm.pbf` behind `-DRFTRACE_ENABLE_OSMIUM=ON` via libosmium), **MSI** antenna
patterns, and **GeoTIFF/DEM terrain** (behind `-DRFTRACE_ENABLE_GDAL=ON`). A scene georeference
(`set_geo_origin` / `geo_project`) projects all geospatial data into the local Z-up ENU frame.

**Core export:** JSON, CSV, GeoJSON, glTF, **CZML** (Cesium), **3D Tiles** (single-tile and a
hierarchical-LOD quadtree via `exportPaths3DTilesLod`), plus **GeoTIFF heatmap** (GDAL) and
**Parquet** (Arrow), both flag-gated.

**Python:** the `rftracekit` package exposes the importers/exporters (`scene.load_geojson`/
`load_cityjson`/`load_osm`/`load_terrain`, `rf.load_msi_antenna`, `result.to_czml`/`to_3dtiles`/
`to_geotiff`/`to_parquet`), the **route** simulation (`Simulator.run_route` → `RouteResult` with
per-sample Doppler), and **MIMO** (`rf.mimo.channel_matrix`/`capacity`/`per_stream_sinr`).
GDAL/Parquet-gated functions are present only when the extension is built with those flags;
`rf.gdal_available()` / `rf.parquet_available()` probe at runtime. Build all optional IO with
`just io` (GDAL + Parquet + libosmium).

Not yet built: a batched simulator path so GPU backends accelerate a full run (the loops are
still per-ray), a general multi-edge/wedge UTD path model (the current selectable UTD reuses the
dominant edge as a half-plane), an Embree adapter, CLI tools, Swift/C bindings, and a CI
workflow. See `openspec/project.md` for the full roadmap.

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
