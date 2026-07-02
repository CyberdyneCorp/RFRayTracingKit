## 1. Settings & Mode Plumbing

- [ ] 1.1 Extend `SimulationSettings` with `mode` (image-method | ray-launch), `seed`, and coverage-grid fields; defaults preserve Phase 1 behavior
- [ ] 1.2 Route `Simulator::run` to the selected propagation mode; keep the image method as the default point-receiver path

## 2. Stochastic Ray Launch (`stochastic-raylaunch`)

- [ ] 2.1 Implement uniform sphere sampling (Fibonacci sphere) with deterministic seeding
- [ ] 2.2 Implement multi-bounce specular ray tracing against the BVH with a power floor and `maxReflections` bound
- [ ] 2.3 Implement the receiver capture sphere and path reconstruction from bounce history
- [ ] 2.4 Implement path deduplication by ordered reflecting-triangle signature
- [ ] 2.5 Aggregate captured paths with the existing RF channel model
- [ ] 2.6 Tests: ray count/tracing, capture in/out of radius, dedup collapses duplicates, same-seed reproducibility

## 3. Multi-Bounce in Both Modes (`ray-simulation`)

- [ ] 3.1 Verify/extend the image method for `maxReflections` > 1 (two-bounce path construction)
- [ ] 3.2 Tests: two-bounce path found (image method), depth bound respected in both modes

## 4. Coverage Grid (`coverage-grid`)

- [ ] 4.1 Define the coverage grid (origin, cell size, W×H, height) and cell enumeration
- [ ] 4.2 Implement coverage-grid mode by evaluating each cell centre as a receiver
- [ ] 4.3 Produce the H×W power array with a no-signal sentinel and retain grid georeference
- [ ] 4.4 Tests: grid sizing, coverage array shape, no-signal sentinel, georeference retained

## 5. Coverage Result & Export (`results-export`)

- [ ] 5.1 Add the coverage result model (power array + optional path-loss array + grid metadata)
- [ ] 5.2 Implement coverage JSON export (grid metadata + row-major values)
- [ ] 5.3 Implement coverage CSV export (long `row,col,x,y,power` table + no-signal sentinel)
- [ ] 5.4 Tests: coverage JSON contains grid+values, CSV one-value-per-cell, round-trip

## 6. GeoJSON Export (`geojson-export`)

- [ ] 6.1 Implement receiver-points GeoJSON (power/loss/delay-spread properties)
- [ ] 6.2 Implement ray-path LineString GeoJSON (type/power/reflections properties)
- [ ] 6.3 Implement coverage-cell GeoJSON (per-cell power; no-signal rule)
- [ ] 6.4 Tests: valid FeatureCollection structure, feature geometry/properties, re-parse

## 7. glTF Export (`gltf-export`)

- [ ] 7.1 Implement an in-tree glTF 2.0 writer (nlohmann/json + embedded base64 buffer) for line primitives (one polyline per path)
- [ ] 7.2 Add per-vertex color derived from path power; optional receiver points
- [ ] 7.3 Tests: exported glTF re-imports via Assimp and exposes the geometry

## 8. Golden & Validation

- [ ] 8.1 Golden: sweep ray count vs capture radius on the Phase 1 golden scenes, pin the smallest budget reaching ≤1 dB agreement with the image method as test constants, and record the trade-off in the README
- [ ] 8.2 Golden: coverage grid over a small scene with a stable expected pattern
- [ ] 8.3 Add an `examples/coverage_grid` demo (coverage mode + GeoJSON/glTF export)
- [ ] 8.4 Update README/docs with the new modes, exporters, and tolerances
- [ ] 8.5 `just ci` and `just spec` pass end-to-end
