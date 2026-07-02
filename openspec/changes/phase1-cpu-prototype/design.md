## Context

RFTraceKit is greenfield. Its long-term value depends on a shared, backend-agnostic RF
core that GPU backends merely accelerate. Phase 1 builds the CPU reference that fixes the
public data model (Scene / Material / Transmitter / Receiver / Result), the RF physics,
and the ray-tracing contract (BVH closest-hit + occlusion) so later phases plug in behind
stable interfaces. Constraints: C++20, Eigen for math, NanoRT-style BVH (small, portable,
no hardware-RT assumption), Assimp + nlohmann/json for I/O, and **no Python/GPU
dependency in the core**.

## Goals / Non-Goals

**Goals:**
- A compilable C++20 library with CMake + optional backend flags and a GoogleTest suite.
- Backend-agnostic scene, RF-physics, and result models that will not need to change to add
  Metal/CUDA/OpenCL.
- A NanoRT-style BVH with closest-hit and occlusion queries validated against brute force.
- Correct RF formulas (FSPL, Fresnel reflection, penetration, phase, delay, antenna gain,
  aggregation) with unit tests against analytic references.
- Point-receiver simulation producing LOS + specular reflection paths (image method) up to
  `maxReflections`, plus JSON/CSV export.
- Golden tests for empty scene, single-wall reflection, and street-canyon cases.

**Non-Goals:**
- GPU backends (Metal/CUDA/OpenCL) and Embree beyond an optional gated stub.
- Python bindings, terrain/GeoTIFF import, coverage grid, diffraction, atmospheric/
  vegetation attenuation, MIMO/beamforming, route simulation.
- GeoJSON/CZML/glTF result export (Phase 2) — only JSON/CSV in Phase 1.

## Decisions

### D1. NanoRT-style BVH over Embree as the reference
A small in-tree SAH BVH keeps the core portable and dependency-light, and gives a
deterministic reference for validating GPU backends. **Alternative:** make Embree the
default — rejected because Embree is CPU/Intel-oriented and unsuitable as the shared
reference for Metal/iPad; it stays behind `RFTRACE_ENABLE_EMBREE` as an optional
validation/perf backend only.

### D2. Backend interface boundary
Define a `Backend` interface whose responsibilities are limited to: build acceleration
structure, closest-hit query, occlusion query, and (optionally) bulk RF post-processing.
Scene loading, material preprocessing, path assembly bookkeeping, and result export stay in
shared CPU code. This matches the core principle "keep RF physics independent from the ray
backend" and lets `SimulationSettings.backend` select an implementation at runtime.
**Alternative:** compile-time backend templating — rejected as it complicates Python/runtime
selection later.

### D3. Specular reflections via the image method
Reflection paths are found by mirroring the source across candidate reflecting planes
(recursively up to `maxReflections`), then validating: the reflection point must lie inside
its triangle, and every segment must pass an occlusion query. This is exact for planar
specular reflection and deterministic. **Alternative:** stochastic ray shooting with a
receiver capture sphere — deferred to Phase 2 for multipath/coverage; the capture radius is
still exposed in settings for forward compatibility. Phase 1 emphasizes the deterministic
image method for correctness and golden tests.

### D4. Double precision in the core
Positions, RF math and geometry use `double` for accuracy and reproducible cross-backend
comparison; interchange/export buffers may narrow to `float` where the spec's zero-copy
layout calls for it in later phases.

### D5. Fresnel-based reflection with constant-loss fallback
Reflection loss is computed from Fresnel coefficients using complex permittivity
(`εr − j·σ/(ωε0)`), polarization and incidence angle, falling back to the material's
`reflectionLossDb` when permittivity is not provided. This keeps simple material tables
usable while enabling physically-based results.

### D6. Result model mirrors the spec's JSON
`RFPath` and per-receiver aggregation map directly onto the example result JSON (§15 of the
source spec) so the JSON exporter is a thin serializer and external tools get a stable
schema.

## Risks / Trade-offs

- **BVH correctness/precision bugs** → validate every query against a brute-force reference
  in tests; use an epsilon offset on occlusion endpoints to avoid self-occlusion.
- **Image-method combinatorial blow-up** on large scenes (candidate planes grow with
  bounce depth) → bound by `maxReflections`, restrict Phase 1 golden scenes to small/medium
  meshes, and defer scalable multipath to Phase 2.
- **Assimp import variance** (units, axis conventions, non-triangulated faces) → force
  triangulation on import and document the expected axis/unit convention in `scene-import`.
- **Floating-point divergence across future backends** → fix double precision and
  deterministic ordering now so CPU-vs-GPU consistency tests have a stable baseline.
- **Interface churn if boundaries are wrong** → the `Backend` interface is intentionally
  minimal (traverse + occlude) to reduce the surface later phases depend on.

## Migration Plan

Greenfield — no migration. Deliverable is the initial library, tests, and two example
programs (`simple_los`, `city_reflection`). Later phases extend behind the interfaces
established here; the archived Phase 1 specs become the baseline in `openspec/specs/`.

## Resolved Decisions

### D7. Test framework: GoogleTest + GMock
GoogleTest is wired via CMake `FetchContent` (or the vcpkg `gtest` port). GMock is used to
fake the `Backend` interface, which sets up the CPU-vs-GPU consistency tests planned for
later phases. Chosen over Catch2 for ecosystem maturity and built-in mocking; Catch2's
BDD sections were attractive for scenario mapping but do not cover the backend-mock need.

### D8. Dependency manager: vcpkg (manifest mode)
Dependencies are declared in a `vcpkg.json` manifest and consumed via the vcpkg CMake
toolchain file, keeping the build CMake-native. All Phase 1 deps (Eigen, Assimp,
nlohmann/json, gtest) are stock vcpkg ports; later cross-compilation for iPad/Android uses
vcpkg triplets (`arm64-ios`, `arm64-android`). Conan was the alternative for heavier
cross-compile profile management and can be revisited if the multi-platform matrix outgrows
triplets, but switching is costly so vcpkg is the committed baseline.

### D10. Developer task runner: `just`
A root `justfile` is the canonical entry point for build/test/validate workflows, matching
the sibling CyberdyneCorp projects (NumPP, SciPP) so contributors moving between repos use
the same commands. It wraps the CMake + vcpkg toolchain and exposes the shared recipe
surface (`configure`, `build`, `lib`, `test`/`unit`, `ctest`, `gcc`, `asan`, `spec`, `ci`,
`install`, `clean`) plus RFTraceKit-specific `example`/`examples` and an optional `embree`
validation-backend recipe. The `spec` recipe runs `openspec validate --all --strict` and is
part of `ci`, keeping the OpenSpec gate in the local loop. **Alternative:** Makefile or
shell scripts — rejected for cross-repo inconsistency and worse ergonomics.

### D9. Internal up-axis: Z-up, with glTF/OBJ converted on import
The core uses a right-handed **Z-up** frame (Z = height/elevation). This matches the source
spec's own coordinate examples (transmitter `[120, 80, 35]`, receiver `[300, 180, 1.5]`
where the third component is height), GIS/geospatial data (GeoTIFF elevation, CityJSON),
and downstream Cesium/CZML/QGIS export — where most RF output goes. glTF is Y-up (and OBJ
is conventionally Y-up in graphics tools), so the importer applies a Y-up→Z-up rotation on
load. The convention and the import rotation are documented in the `scene-import` spec and
enforced by tests.
