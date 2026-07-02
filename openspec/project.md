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

1. **CPU Prototype** *(current change: `phase1-cpu-prototype`)* — scene model, mesh/material
   import, NanoRT-style BVH, LOS + basic reflection, FSPL, JSON/CSV export.
2. **RF Multipath** — multi-bounce reflections, material loss, phase/delay, receiver
   capture sphere, coverage grid, GeoJSON/glTF export.
3. **Python Bindings & Visualization** — pybind11 module, Scene/Simulator/Result
   wrappers, NumPy/Pandas, PyVista/Plotly helpers.
4. **Metal Backend** — MTLAccelerationStructure, iPad/macOS compute kernels, Swift bridge.
5. **CUDA Backend** — OptiX ray tracing + CUDA RF kernels.
6. **OpenCL Backend** — portable BVH traversal for non-NVIDIA GPUs.
7. **Advanced RF** — diffraction, rain/vegetation attenuation, antenna arrays, MIMO
   channel matrix, SINR/cell planning, moving receivers.

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
