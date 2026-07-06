# Changelog

All notable changes to RFTraceKit are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project aims to follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html) (pre-1.0: minor/patch may still
carry breaking changes while the API stabilizes).

## [Unreleased]

## [0.3.0] - 2026-07-06

### Added
- **Installable package** — `install`/`export` rules and a generated `RFTraceKitConfig.cmake`
  so downstream projects can `find_package(RFTraceKit)` and link `rftrace::rftrace` (and
  `rftrace::rftrace_c` when the C API is built). The config re-resolves the transitive
  CONFIG-mode dependencies (Eigen3, Assimp, nlohmann/json) and ships a
  `SameMajorVersion` `RFTraceKitConfigVersion.cmake`.
- **Consumability spec** — a portable OpenSpec "usable by others" readiness rubric
  (`openspec/specs/consumability`) consolidating the install/packaging/CI/versioning/governance
  requirements, reusable as an adoption checklist for other projects.
- **Project governance** — `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, this
  changelog, and GitHub issue/PR templates.
- **Versioning & API-stability policy** and an installed-package "how to consume" guide in the
  README.

### Changed
- `vcpkg.json` version realigned with the project version (was stale at `0.1.0`).

## [0.2.0] - 2026-07-05

### Added
- **`just gpu-detect`** — cross-platform (Linux/macOS/Windows) probe that reports usable ray
  backends (CUDA/OptiX, OpenCL, Metal) and recommends the matching build recipe, falling back to
  the always-available CPU backend.
- Python: `thread_count`, UTD diffraction model, and depolarization exposed, plus a showcase.

## [0.1.0] - 2026-07-04

Initial tagged release — a modern C++20 ray tracing and RF propagation library.

### Added
- **RF physics** — LOS + reflection, FSPL/path loss, delay, phase, polarization, UTD
  diffraction (single- and multi-edge), atmospheric/vegetation attenuation, Doppler, and MIMO
  channel math, all in a backend-agnostic core.
- **Simulation modes** — point-to-point, stochastic ray launch + capture sphere with
  multi-bounce multipath, coverage grids, and route/drive-test simulation.
- **Ray backends** — pure-C++20 NanoRT-style BVH (fp64, always available, reference oracle);
  optional Embree 4, CUDA/OptiX, Metal, and OpenCL backends, each validated against the CPU
  oracle within a documented fp32 tolerance.
- **Scene import** — glTF/OBJ/FBX (Assimp), OSM/OSM-PBF, GeoJSON, CityJSON, and GeoTIFF/DEM
  terrain; materials, antennas, and georeferencing.
- **Export** — JSON, CSV, GeoJSON, glTF, CZML, 3D Tiles (with LOD), Parquet, and GeoTIFF
  heatmaps for Cesium/QGIS/Blender/Python workflows.
- **Bindings & CLI** — Python (`rftracekit`, NumPy/pandas, PyVista/Plotly helpers) over
  pybind11, a stable `extern "C"` C ABI, Swift bindings, and command-line tools.
- **Tooling** — a `justfile` (build, test, GPU, examples, CLI, spec), a GitHub Actions CI
  pipeline (Linux/clang core + CLI + C API + OpenSpec validation), and in-repo OpenSpec specs.

[Unreleased]: https://github.com/CyberdyneCorp/RFRayTracingKit/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/CyberdyneCorp/RFRayTracingKit/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/CyberdyneCorp/RFRayTracingKit/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/CyberdyneCorp/RFRayTracingKit/releases/tag/v0.1.0
