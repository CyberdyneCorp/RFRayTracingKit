## Context

Phases 1–7 give a validated, backend-agnostic CPU RF core: `Scene` (meshes, materials,
transmitters, receivers, `CoordinateSystem`), `AntennaPattern`, coverage grids, `RFResult`
paths, and a family of exporters (`json`/`csv`/`geojson`/`gltf`) plus importers (Assimp mesh,
JSON material) in the `io::` namespace. Geospatial import/export currently lives only in Python
example scripts. This change moves those capabilities into the C++ core so any caller — not just
Python — can load georeferenced scenes from open data and write Cesium/QGIS/analytics outputs.

The core stays right-handed **Z-up metres, double precision, Python-free**. JSON IO uses the
existing nlohmann/json dependency and is always built. GDAL and Arrow/Parquet are optional,
default-OFF, and must not perturb the default build (the primary regression guard: the current
125 C++ tests stay green).

## Goals / Non-Goals

**Goals:**
- One shared georeference + ENU projection driving every geospatial importer, so all imported
  data lands in one consistent local frame.
- Always-on JSON import (GeoJSON, CityJSON, OSM/Overpass) and export (CZML, 3D Tiles) needing
  only nlohmann/json.
- MSI/PLN antenna import producing a 2D (H+V) pattern.
- Flag-gated GDAL (GeoTIFF terrain + heatmap) and Arrow (Parquet result table) IO that is
  always declared and fails loudly-but-gracefully when not compiled.

**Non-Goals:**
- OSM PBF, hierarchical 3D-Tiles LOD, arbitrary CRS reprojection, full geodetic (ellipsoidal)
  projection, vector-tile/WMS services, GPU involvement. Terrain is a heightfield triangulation,
  not adaptive/quadtree LOD.

## Decisions

### D1. Georeferencing: scene geographic origin + shared equirectangular ENU projection
Add a geographic origin (`lat0`,`lon0`) to the scene and a single ENU projection used by ALL
geospatial importers:

```
x = (lon - lon0) * 111320 * cos(lat0)
y = (lat - lat0) * 110540
z = altitude
```

right-handed, Z-up, metres. API: `Scene::setGeoOrigin(lat, lon)`, `Scene::hasGeoOrigin()`,
`Scene::geoProject(lat, lon, alt) -> Vec3`. If no origin is set when a geospatial importer runs,
the importer sets it to the dataset's centroid before projecting. The origin and
`georeferenced=true` (plus `originLat`/`originLon`) are recorded on `CoordinateSystem`.
Rationale: equirectangular is exact enough for city-scale scenes, is what the Python examples
already used, is trivially invertible for export, and needs no external projection library.

### D2. Shared footprint-extrusion helper
A single core helper turns a polygon footprint (ring of `Vec3` at a base elevation + a height)
into wall triangles (two per edge) plus a flat-roof triangle fan/triangulation. Reused by
GeoJSON, CityJSON, and OSM importers so extrusion behavior (winding, roof, closing the ring) is
defined once. Rationale: DRY; one place to test winding/normals.

### D3. JSON always-on; GDAL and Parquet flag-gated with graceful degradation
GeoJSON/CityJSON/OSM/MSI/CZML/3D-Tiles need only nlohmann/json and are always compiled. GDAL
(`RFTRACE_ENABLE_GDAL` → `RFTRACE_HAVE_GDAL`) and Parquet (`RFTRACE_ENABLE_PARQUET` →
`RFTRACE_HAVE_PARQUET`) mirror the existing `RFTRACE_ENABLE_EMBREE`/`find_package` +
`target_compile_definitions RFTRACE_HAVE_*` pattern, default OFF. The gated `.cpp` is compiled
only when ON. The public API is ALWAYS declared; when the feature is absent it throws a clear
`"built without GDAL"` / `"built without Parquet"` error, and a `backendAvailable`-style helper
(e.g. `gdalAvailable()` / `parquetAvailable()`) reports availability at runtime. Gated tests are
`#if RFTRACE_HAVE_*` and become no-ops otherwise, so the default build's 125 tests stay green.

### D4. OSM import consumes Overpass JSON (not PBF)
Parse the Overpass JSON that `fetch_osm.py` already downloads. Ways/relations with a `building`
tag → extruded buildings (height from `height`, else `building:levels` × storey height, else a
documented default). `natural=wood` / `landuse=forest` / `leisure=park` → vegetation geometry.
Node coordinates are resolved from the element table, then projected via D1. Rationale: matches
the existing data pipeline; PBF decoding is a large dependency and is out of scope.

### D5. MSI antenna import → AntennaPattern with H and V cuts
Parse the Planet/MSI `.msi`/`.pln` text format: `NAME`, `FREQUENCY`, `GAIN` (dBi/dBd),
`HORIZONTAL <n>` and `VERTICAL <n>` blocks of `angle attenuation` rows. Extend `AntennaPattern`
with a `verticalCutDb` table alongside the existing `azimuthCutDb`; `gainTowards` combines the H
and V attenuations with the peak gain (documented combination rule, e.g. sum of the two cut
attenuations relative to boresight). Rationale: vendor patterns are 2D; azimuth-only loses the
elevation roll-off that matters for downtilt planning.

### D6. CZML export
Emit a Cesium CZML JSON document: a document packet plus one point packet per receiver and one
polyline packet per ray path. Positions use `cartographicDegrees` (lon, lat, height) when the
scene is georeferenced (inverse of D1), else `cartesian`. Rationale: CZML is the native
time/entity format for CesiumJS and carries per-entity styling and metadata.

### D7. 3D Tiles export reuses the glTF path exporter
Emit a minimal but valid 3D Tiles 1.1 `tileset.json` (asset version, geometricError, a single
root tile with a bounding volume and a `content.uri`) referencing ONE glTF/glb produced by the
existing `gltf_exporter`. Documented as a single-tile tileset with no hierarchical LOD.
Rationale: reuse the validated glTF writer; a single tile is enough to load in CesiumJS.

### D8. GeoTIFF terrain (GDAL) and Parquet (Arrow) payloads
- **DEM import** returns a terrain height field, a triangulated terrain mesh (grid of the raster
  posts projected to local ENU via D1 using the raster geotransform), and an elevation sampler
  `elevationAt(x, y)`; elevations are absolute metres (z).
- **Heatmap export** writes a `CoverageResult` to a georeferenced single-band GeoTIFF (one band
  of the chosen metric, geotransform derived from the grid origin/cell size and the scene
  georeference, a no-data sentinel for uncovered cells).
- **Parquet export** writes the per-receiver table `(id, x, y, z, received_power_dbm,
  path_loss_db, delay_spread_ns)` via Arrow to a Parquet file.

## Risks / Trade-offs

- **Equirectangular distortion** grows with scene extent and latitude → documented as
  city-scale-accurate; a full projection library is a non-goal.
- **Optional-dependency drift** (default build must stay green) → gated code is compile-excluded
  and gated tests are `#if RFTRACE_HAVE_*` no-ops; the 125-test default build is the CI guard.
- **MSI dialect variance** (`.msi` vs `.pln`, dBd vs dBi, comment styles) → parse the common
  NAME/FREQUENCY/GAIN/HORIZONTAL/VERTICAL grammar, normalize gain to dBi, and error clearly on
  unrecognized structure.
- **3D-Tiles minimality** → single-tile only; large-scene LOD is explicitly deferred.
- **OSM height heuristics** (missing tags) → documented fallback (levels × storey height, then a
  default height) so buildings are never dropped silently.

## Migration Plan

Purely additive over Phases 1–7; no breaking API changes. New IO is new files under
`src/{importers,exporters}/` and `include/rftrace/{importers,exporters}/`. `Scene` gains
georeference members (defaulting to "no origin"), and `AntennaPattern` gains `verticalCutDb`
(empty by default → identical omni/azimuth-only behavior). GDAL/Parquet stay OFF by default so
existing builds and the 125 tests are unaffected. Recommended implementation order:
1. `georeferencing` + the D2 footprint-extrusion helper (foundation for all importers).
2. `geojson-import`, then `cityjson-import`, then `osm-import` (share D1/D2).
3. `antenna-msi-import` (AntennaPattern H+V).
4. `czml-export`, then `tiles3d-export` (reuse glTF writer).
5. Gated `geotiff-terrain`, `geotiff-heatmap-export` (GDAL) and `parquet-export` (Arrow).

## Resolved Decisions

All eight decisions D1–D8 above are resolved and to be implemented as written; there are no open
questions. Key confirmations:
- **Projection:** equirectangular ENU (D1) is the single projection for all importers.
- **Origin default:** dataset centroid when unset at import time (D1).
- **Extrusion:** one shared helper (D2), flat roofs only.
- **Gating:** JSON always on; GDAL/Parquet default OFF, declared-and-throwing when absent, with
  availability helpers (D3).
- **OSM:** Overpass JSON only, no PBF (D4).
- **Antenna:** MSI/PLN with combined H+V cut in `gainTowards` (D5).
- **CZML/3D-Tiles:** CZML packets (D6); single-tile 3D-Tiles reusing the glTF writer (D7).
- **GDAL/Arrow payloads:** DEM → heightfield+mesh+sampler; coverage → single-band GeoTIFF;
  per-receiver table → Parquet (D8).
