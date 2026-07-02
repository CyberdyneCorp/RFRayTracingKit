## 1. Project Scaffold & Build

- [x] 1.1 Create repository layout: `include/rftrace/`, `src/{core,rf,importers,exporters,backends/cpu_nanort}`, `tests/`, `examples/`
- [x] 1.2 Author root `CMakeLists.txt` targeting C++20 with the optional backend flags (`RFTRACE_ENABLE_PYTHON/METAL/CUDA/OPENCL/EMBREE`, all defaulting to OFF except CPU)
- [x] 1.3 Add a `vcpkg.json` manifest and wire the vcpkg CMake toolchain for Eigen, Assimp, nlohmann/json, and gtest (test-only)
- [x] 1.4 Add the GoogleTest + GMock harness and a CI-friendly `ctest` target; add an OpenSpec `openspec validate --all --strict` CI step
- [x] 1.5 Add a root `justfile` (CyberdyneCorp/NumPP-SciPP convention): `configure`, `build`, `lib`, `test`(+`unit`), `ctest`, `example`/`examples`, `debug`, `gcc`, `asan`, `embree`, `spec`, `ci`, `install`, `clean`; wire the vcpkg toolchain and `RFTRACE_*` flags

## 2. Core Geometry (`core-geometry`)

- [x] 2.1 Define Eigen-backed `Vec3`, `Ray` (origin, direction, tMin/tMax), `Triangle`, `AABB` in `include/rftrace/`
- [x] 2.2 Implement Möller–Trumbore ray–triangle intersection returning `t`, barycentric `(u,v)`, triangle index; handle parallel/degenerate cases
- [x] 2.3 Implement NanoRT-style BVH construction over a triangle set — median split on the widest centroid axis (including empty-mesh case); SAH is a future optimization
- [x] 2.4 Implement closest-hit traversal
- [x] 2.5 Implement occlusion (any-hit) traversal with endpoint epsilon offset
- [x] 2.6 Tests: intersection hit/miss/degenerate; BVH vs brute-force closest-hit; occlusion blocked/clear/self-intersection

## 3. Scene Model (`scene-model`)

- [x] 3.1 Define `Material` and built-in presets (concrete, brick, glass, metal, wood, water, vegetation, asphalt, soil)
- [x] 3.2 Define `AntennaPattern` (omnidirectional + tabulated directional lookup) and `Polarization`
- [x] 3.3 Define `Transmitter`, `Receiver`, `MeshCollection` (per-triangle material resolution), `CoordinateSystem`
- [x] 3.4 Define `Scene` container with add/lookup APIs; enforce unique transmitter/receiver ids and default-material behavior
- [x] 3.5 Tests: scene assembly, duplicate-id rejection, hit→material resolution, default material, omni/directional gain

## 4. Scene Import (`scene-import`)

- [x] 4.1 Implement Assimp-based `loadMesh` for glTF/OBJ with forced triangulation, Y-up→Z-up normalization, and triangle-count reporting
- [x] 4.2 Implement material assignment on import (by name), rejecting unknown material names
- [x] 4.3 Implement `loadMaterials` from JSON (nlohmann/json) with descriptive validation errors
- [x] 4.4 Tests: load glTF and OBJ fixtures, quad triangulation, Y-up→Z-up rotation (height maps to Z), missing-file error, materials JSON load + malformed-input error

## 5. RF Propagation (`rf-propagation`)

- [x] 5.1 `rf/free_space_path_loss.hpp`: FSPL with zero-distance guard
- [x] 5.2 `rf/fresnel.hpp`: complex-permittivity Fresnel coefficients (TE/TM), PEC limit
- [x] 5.3 `rf/reflection.hpp`: reflection loss from Fresnel with `reflectionLossDb` fallback
- [x] 5.4 `rf/penetration.hpp`: material penetration loss
- [x] 5.5 `rf/phase.hpp`: phase accumulation and propagation delay
- [x] 5.6 `rf/antenna_pattern.hpp`: apply Tx/Rx antenna gains from directions
- [x] 5.7 `rf/channel.hpp`: per-path received power and multipath aggregation (incoherent power sum + coherent complex sum)
- [x] 5.8 Tests: FSPL reference (3.5 GHz @ 1 km ≈103.3 dB) and 6 dB scaling; Fresnel PEC limit; phase(1λ)=2π; delay(300 m)≈1 µs; incoherent/coherent aggregation

## 6. Ray Simulation (`ray-simulation`)

- [x] 6.1 Define the `Backend` interface (build AS, closest-hit, occlusion, optional bulk RF) and `Backend` selection enum
- [x] 6.2 Implement the CPU (NanoRT) backend against the interface
- [x] 6.3 Define `SimulationSettings` (backend, maxReflections, raysPerTransmitter, captureRadius) with valid defaults and unavailable-backend handling
- [x] 6.4 Implement point-receiver mode iterating (transmitter, receiver) pairs, recording no-signal receivers
- [x] 6.5 Implement LOS path finding via occlusion query
- [x] 6.6 Implement specular reflection path finding via the image method up to `maxReflections`, validating in-triangle bounce points and per-segment occlusion
- [x] 6.7 Ensure deterministic ordering/output for reproducibility
- [x] 6.8 Tests: LOS obstructed/clear, single-wall reflection found, invalid-image rejected, bounce-count bound, repeated-run determinism

## 7. Results & Export (`results-export`)

- [x] 7.1 Define `RFPath` and per-receiver aggregated `RFResult` (power, path loss, delay spread, contributing paths)
- [x] 7.2 Implement per-receiver aggregation (power combining + delay spread)
- [x] 7.3 Implement JSON exporter matching the result schema, plus a loader for round-trip
- [x] 7.4 Implement CSV exporter (one row per receiver, no-signal sentinel)
- [x] 7.5 Tests: LOS/reflection path records, delay spread, JSON schema + round-trip, CSV rows + no-signal sentinel

## 8. Examples & Golden Tests

- [x] 8.1 `examples/simple_los`: single transmitter/receiver, LOS, JSON/CSV export
- [x] 8.2 `examples/city_reflection`: transmitter/receiver with a wall producing a reflection path
- [x] 8.3 Golden tests: empty scene, single-wall reflection, two-building canyon; assert stable power/loss/delay outputs
- [x] 8.4 Update `README`/`docs` with build instructions, backend flags, and the public C++ API usage example

## 9. Validation

- [x] 9.1 `just spec` (`openspec validate --all --strict`) passes
- [x] 9.2 `just ci` passes end-to-end (clang tests + gcc + asan + spec + examples); document CPU rays/sec baseline from a golden scene
