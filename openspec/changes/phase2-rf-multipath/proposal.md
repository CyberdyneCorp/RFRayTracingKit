## Why

Phase 1 produces exact line-of-sight and specular-reflection paths for known point
receivers via the image method — correct, but it does not scale to rich urban multipath
(the image method's candidate count explodes with bounce depth) and it cannot answer the
most common planning question: "what is the coverage over an area?" Phase 2 adds a
Monte-Carlo ray-launch propagation engine with a receiver capture sphere, multi-bounce
reflections, a coverage-grid mode, and web/GIS-ready GeoJSON/glTF export — turning the
reference core into a usable coverage tool while keeping the image method as the
correctness oracle.

## What Changes

- Add a **stochastic ray-launch** propagation mode: launch `raysPerTransmitter` rays over
  the transmitter sphere, trace multi-bounce specular reflections against the BVH, and
  capture rays that pass within a receiver's capture sphere; deduplicate near-identical
  paths and aggregate them with the existing RF channel model.
- Extend **ray-simulation** so `SimulationSettings` selects between the deterministic image
  method (Phase 1) and stochastic ray launch, and so multi-bounce depth (`maxReflections`
  > 1) is a first-class, tested path for both.
- Add a **coverage-grid** simulation mode: evaluate received power/path loss on a regular
  horizontal grid at a fixed height, producing a 2D coverage array plus grid georeference.
- Extend **results-export** with a coverage result (2D array + grid metadata) and its
  CSV/JSON serialization.
- Add **GeoJSON export** for receiver points, ray-path lines, and coverage cells.
- Add **glTF export** for debug ray-path line geometry (colored by power) and receiver
  points, for Blender/WebGL inspection.
- Add golden tests comparing stochastic ray-launch aggregate power against the image-method
  reference on the Phase 1 golden scenes (within tolerance), plus coverage-grid and
  exporter tests.

## Capabilities

### New Capabilities
- `stochastic-raylaunch`: Monte-Carlo ray launching with multi-bounce specular tracing and
  receiver-capture-sphere aggregation.
- `coverage-grid`: area coverage mode producing a 2D received-power/path-loss grid with
  georeference metadata.
- `geojson-export`: GeoJSON serialization of receivers, ray paths, and coverage cells.
- `gltf-export`: glTF export of debug ray-path geometry and receiver points.

### Modified Capabilities
- `ray-simulation`: add propagation-mode selection (image method vs ray launch) and
  first-class multi-bounce (`maxReflections` > 1) behavior.
- `results-export`: add the coverage result data model and its JSON/CSV export.

## Impact

- **Code:** new `src/backends/cpu_nanort/raylaunch.*`, `src/core/coverage.*`,
  `src/exporters/geojson_exporter.*`, `src/exporters/gltf_exporter.*`; extends
  `Simulator`, `SimulationSettings`, `RFResult`.
- **Dependencies:** glTF export reuses Assimp (already a dependency) or a lightweight
  writer; no new required dependency. Determinism relies on a seeded RNG carried in
  settings.
- **Out of scope (later phases):** GPU backends, Python bindings, terrain/GeoTIFF,
  diffraction, atmospheric/vegetation attenuation, MIMO/beamforming, route/moving-receiver
  simulation, CZML/3D-Tiles export.
