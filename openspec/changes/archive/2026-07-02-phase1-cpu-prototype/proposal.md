## Why

RFTraceKit needs a correct, portable foundation before any GPU acceleration or Python
tooling can be trusted. GPU backends (Metal, CUDA, OpenCL) are only useful if a
backend-agnostic CPU reference exists to validate them against, and the RF physics must
be proven correct in isolation from ray-traversal hardware. Phase 1 delivers that
reference: a C++20 CPU engine that loads a scene, casts rays over a NanoRT-style BVH,
computes line-of-sight and reflected propagation paths with real RF formulas, and
exports results for external analysis.

## What Changes

- Establish the C++20 project skeleton: CMake build with optional backend feature flags,
  public headers under `include/rftrace/`, GoogleTest harness, dependency management
  (Eigen, Assimp, nlohmann/json) via vcpkg, and a root `justfile` task runner following the
  CyberdyneCorp convention (NumPP/SciPP).
- Introduce the backend-agnostic **scene model** (Scene, MeshCollection, Material,
  Transmitter, Receiver, AntennaPattern, Polarization, CoordinateSystem) and its
  construction API.
- Introduce **core ray-tracing geometry**: Eigen-based Vec3/Ray/Triangle/AABB and a
  NanoRT-style BVH supporting closest-hit and any-hit (occlusion) queries.
- Introduce **scene import** for triangle meshes (glTF, OBJ via Assimp) and materials
  (JSON), with material assignment to meshes.
- Introduce the **RF propagation** formula modules: free-space path loss, reflection
  loss (Fresnel-based), material penetration loss, phase accumulation, delay, antenna
  gain lookup, and per-receiver power aggregation.
- Introduce the **ray simulation** engine: a `Backend` abstraction with a CPU backend,
  point-receiver simulation mode, LOS path finding (occlusion test) and specular
  reflection path finding (image method up to `maxReflections`), driven by
  `SimulationSettings`.
- Introduce **results export**: RFPath/RFResult data model, per-receiver aggregation
  (received power, path loss, delay spread, phase), and JSON + CSV serialization.

## Capabilities

### New Capabilities
- `core-geometry`: Eigen-based ray-tracing primitives and NanoRT-style BVH construction
  and traversal (closest-hit and occlusion queries).
- `scene-model`: Backend-agnostic scene graph and its construction API (meshes,
  materials, transmitters, receivers, antenna patterns, coordinate system).
- `scene-import`: Loading triangle meshes (glTF/OBJ) and material definitions (JSON)
  into the scene model.
- `rf-propagation`: RF physics formula modules (FSPL, reflection, penetration, phase,
  delay, antenna gain, power aggregation).
- `ray-simulation`: Backend abstraction, CPU backend, and point-receiver simulation
  producing LOS and reflected propagation paths.
- `results-export`: Result data model, per-receiver metric aggregation, and JSON/CSV
  export.
- `dev-tooling`: Root `justfile` task runner (build/test/spec/ci/asan recipes) matching the
  CyberdyneCorp NumPP/SciPP convention.

### Modified Capabilities
<!-- None — greenfield project, no existing specs. -->

## Impact

- **New codebase:** `CMakeLists.txt`, `include/rftrace/`, `src/core/`, `src/rf/`,
  `src/importers/`, `src/exporters/`, `src/backends/cpu_nanort/`, `tests/`, `examples/`.
- **New dependencies:** Eigen, Assimp, nlohmann/json, GoogleTest (test-only). Embree is
  gated behind `RFTRACE_ENABLE_EMBREE` and remains optional.
- **Out of scope (later phases):** GPU backends (Metal/CUDA/OpenCL), Python bindings,
  terrain/GeoTIFF import, coverage grid heatmaps, diffraction, atmospheric/vegetation
  attenuation, MIMO/beamforming, route simulation, GeoJSON/CZML/glTF result export.
