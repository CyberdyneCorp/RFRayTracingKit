# Project: RFTraceKit

## Purpose

RFTraceKit is a modern C++20 library for **general ray tracing** and **RF propagation
simulation** for 4G / 5G / 6G and above. It takes a 3D scene (buildings, terrain,
materials) plus transmitters and receivers and computes radio propagation paths,
signal strength, delay, phase, multipath and coverage data. Results are exportable to
external visualization tools (Cesium, QGIS, Blender, Python notebooks, WebGL apps).

The engine is designed for multiple acceleration backends (CPU, Metal, CUDA/OptiX,
OpenCL) behind a shared RF-physics core, and exposes a first-class Python API via
pybind11.

## Core Architectural Principle

**Keep RF physics independent from the ray backend.** Metal, CUDA and OpenCL only
accelerate ray traversal and bulk RF calculations. The simulation model, scene graph
and result format remain shared, backend-agnostic C++ code. The CPU backend is the
correctness reference against which all GPU backends are validated.

## Tech Stack

- **Language:** C++20 (core), Python 3.9+ (bindings layer)
- **Math:** Eigen (vectors, matrices, complex numbers, antenna/MIMO math)
- **Ray tracing (CPU):** NanoRT-style BVH; optional Embree adapter for validation/perf
- **Scene import:** Assimp (glTF/OBJ/FBX), GDAL (GeoTIFF/DEM terrain — later phase)
- **Serialization:** nlohmann/json (config, materials, results)
- **Python bindings:** pybind11 with NumPy-compatible zero-copy buffers (later phase)
- **Build:** CMake + vcpkg (manifest mode via `vcpkg.json`), with optional backend feature flags
- **Test:** GoogleTest + GMock for C++ unit/golden tests
- **Coordinate frame:** right-handed **Z-up** (Z = height/elevation), matching GIS/Cesium;
  glTF/OBJ (Y-up) meshes are rotated to Z-up on import

## Backend Feature Flags (CMake)

```
-DRFTRACE_ENABLE_PYTHON=ON/OFF
-DRFTRACE_ENABLE_METAL=ON/OFF
-DRFTRACE_ENABLE_CUDA=ON/OFF
-DRFTRACE_ENABLE_OPENCL=ON/OFF
-DRFTRACE_ENABLE_EMBREE=ON/OFF
```

## Development Phases (Roadmap)

Status as of 2026-07-03 (all core work verified by build+test unless noted):

1. **CPU Prototype** ✅ — scene model, mesh/material import, NanoRT-style BVH, LOS +
   reflection, FSPL, JSON/CSV export.
2. **RF Multipath** ✅ — stochastic ray launch + capture sphere, multi-bounce, coverage
   grid, GeoJSON/glTF export.
3. **Python Bindings & Visualization** ✅ — pybind11 `rftracekit`, NumPy/pandas,
   PyVista/Plotly helpers (arrays + advanced-RF settings exposed).
4. **Metal Backend** ✅ — `MTLAccelerationStructure` + batched query API, CPU-vs-Metal parity.
5. **CUDA Backend** ✅ — OptiX + CUDA kernels, flag-gated; **verified on an NVIDIA GeForce
   RTX 5060** (Blackwell, `sm_120`; CUDA 12.0, driver 580.95.05, OptiX SDK 9.0.0) with
   CPU-vs-CUDA parity. Optimised query path (pooled device + pinned host staging, async
   copies) and a caller-owned-output API (`closestHitBatchInto`/`occludedBatchInto`).
6. **OpenCL Backend** ✅ — custom flat-BVH traversal kernel, CPU-vs-OpenCL parity (Apple OpenCL).
7. **Advanced RF** ✅ — knife-edge diffraction, rain/gaseous/vegetation attenuation, antenna
   arrays + beam steering, MIMO channel + capacity, SINR/cell planning, route simulation.

**Beyond the roadmap (also done):**
- **Core geospatial IO** — georeferencing; GeoJSON/CityJSON/OSM/MSI import; GeoTIFF-DEM
  terrain (GDAL) + per-cell terrain-height coverage; CZML, 3D Tiles, GeoTIFF-heatmap, and
  Parquet export. JSON formats always-on; GDAL/Parquet flag-gated.
- **Advanced RF physics** — multi-edge diffraction (Bullington/Deygout, selectable); UTD
  wedge coefficient + transition function (validated primitive); polarization mismatch +
  depolarizing reflection; per-path Doppler (routes); ray-launch multipath coverage
  (reflections). All additive/default-neutral; analytic-reference tested.
- **Python IO surface** — the new importers/exporters, route (`run_route`), and MIMO
  (`rf.mimo`) are exposed to Python; GDAL/Parquet-gated functions probe availability.
- **OSM XML/PBF** import (XML always-on; PBF via libosmium, gated) and **hierarchical-LOD
  3D Tiles** export.
- Worked examples: `wifi_indoor`, `barbados_5g` (single + 6-sector, real OSM + open DEM),
  antenna-clearance analysis.

UTD is now a selectable diffraction path model (`DiffractionModel::UTD`, tracks knife-edge) and
reflection **depolarization** is wired (opt-in via `enableDepolarization`).

The batched query API is **caller-owned-output capable**: `IBackend::closestHitBatchInto` /
`occludedBatchInto` write into a caller-reused `std::span`, allocating nothing for the output (the
vector-returning forms are thin wrappers). The CUDA backend overrides these as its
zero-output-allocation fast path. The **simulator now uses this batched path** for its
independent-ray stages — LOS occlusion (across all receivers/coverage cells) and the ray-launch
wavefront (`closestHit` batched per bounce across live rays) — so a whole run's traversal is a
handful of device dispatches, not per-ray round trips (verified on an RTX 5060; results bit-for-bit
identical to the per-ray path, CPU-neutral). Measured end-to-end, full-run wall time is dominated by
CPU-side path processing (capture/dedup, RF physics, aggregation) and per-run backend construction,
not ray traversal, so GPU vs CPU is ~break-even for typical scenes — the traversal batching was
necessary but the remaining full-run bottleneck is CPU-side.

**CPU-side full-run acceleration** then addressed that bottleneck, all results-preserving
(bit-for-bit, deterministic): (1) an exact **receiver capture spatial index** (uniform grid) that
turns the ray-launch O(rays×receivers) capture scan near-linear — **ray-launch coverage 2.33 s →
0.074 s (~31×)** on a 25.6k-cell grid; (2) **deterministic `threadCount` parallelism** over
independent receivers/cells (disjoint output slots ⇒ schedule-independent, CPU-backend-gated) —
**~2.7–8.5× on reflection-heavy image-method coverage** at 24 cores, with the golden suites passing on
the parallel path by default; (3) **backend reuse across runs** (scene-geometry-hash cache) so
parameter sweeps and coverage+route on one scene skip the per-run acceleration-structure build.

**Known gaps / not yet built:** batching the remaining per-ray query sites
(image-method reflection segments, `buildTerrainProfile` down-rays, diffraction edges) for
traversal-heavy scenes; Embree adapter (flag maps to CPU); general multi-edge/wedge UTD path model
(current UTD reuses the dominant-edge v as a half-plane); Swift bindings + C API; CLI tools
(`rftrace-cli`, `scene-validator`, `result-converter`); CI workflow.

## Project Conventions

- Public headers live under `include/rftrace/`; implementation under `src/`.
- RF physics formula modules live under `src/rf/` (one concern per header:
  `free_space_path_loss.hpp`, `reflection.hpp`, `fresnel.hpp`, `penetration.hpp`,
  `phase.hpp`, `antenna_pattern.hpp`, `channel.hpp`).
- The C++ core MUST NOT depend on Python or any Python-ecosystem library.
- Backends implement a shared `Backend` interface; adding a backend must not require
  changes to RF physics or scene/result code.
- Any bug fix ships with a regression test.
- Keep per-function cognitive complexity within systems-code bands (isolate BVH/AST-style
  traversal with clear structure; document genuinely irreducible algorithms).
