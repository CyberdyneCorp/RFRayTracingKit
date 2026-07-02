## Why

Real RF planning starts from open geospatial data (OpenStreetMap buildings, CityJSON/GeoJSON
city models, terrain DEMs, vendor antenna patterns) and ends in geospatial viewers (CesiumJS,
QGIS) and analytics tools (pandas/GIS). Today RFTraceKit can only reach these formats through
throwaway Python example scripts (`fetch_osm.py`, MSI parsers, GeoTIFF/Parquet helpers): the
backend-agnostic C++ core cannot import a georeferenced scene or export to Cesium/QGIS/Parquet
on its own. That leaves the core unusable from non-Python callers, duplicates parsing logic,
and keeps the coordinate/georeference story informal.

This change promotes those capabilities into the library as first-class, Python-free C++ IO:
a scene georeference and an ENU projection shared by every geospatial importer, always-on
JSON-based import/export (only nlohmann/json, already a dependency), and optional GDAL/Parquet
IO that is flag-gated and degrades gracefully when not compiled.

## What Changes

- **Georeferencing (core):** add a geographic origin (`lat0`/`lon0`) to the scene and a shared
  equirectangular ENU projection used by all geospatial importers. `Scene::setGeoOrigin`,
  `hasGeoOrigin`, `geoProject(lat,lon,alt)`; if no origin is set when a geospatial importer
  runs, the origin defaults to the dataset centroid. State is recorded in `CoordinateSystem`.
- **Footprint extrusion (core helper):** a shared helper that turns a polygon footprint (ring
  + base elevation + height) into wall + flat-roof triangles, reused by GeoJSON/CityJSON/OSM.
- **GeoJSON import (always-on):** Polygon/MultiPolygon building footprints (with a height /
  levels property) → extruded building meshes projected via the georeference.
- **CityJSON import (always-on):** CityJSON `Building`/`BuildingPart` objects → meshes (solid
  boundaries where present, else footprint + height extrusion).
- **OSM import (always-on):** Overpass JSON (what `fetch_osm.py` downloads); `building` ways →
  extruded buildings (height from `height` / `building:levels`, else default), `natural=wood` /
  `landuse=forest` / `leisure=park` → vegetation geometry.
- **MSI antenna import (always-on):** the Planet/MSI `.msi`/`.pln` text pattern format
  (NAME/FREQUENCY/GAIN/HORIZONTAL/VERTICAL angle-attenuation tables) → an `AntennaPattern`
  with both horizontal and vertical cuts; `AntennaPattern` gains a `verticalCutDb` table and
  `gainTowards` combines H and V.
- **CZML export (always-on):** a Cesium CZML JSON document with receiver point packets and
  ray-path polyline packets (cartographicDegrees when georeferenced, else cartesian).
- **3D Tiles export (always-on):** a minimal valid 3D Tiles 1.1 `tileset.json` referencing a
  single glTF/glb produced by the existing glTF path exporter (single-tile, no hierarchical LOD).
- **GeoTIFF terrain import (GDAL, gated):** read a DEM into a terrain height field + a
  triangulated terrain mesh + an elevation sampler.
- **GeoTIFF heatmap export (GDAL, gated):** write a `CoverageResult` to a georeferenced
  single-band GeoTIFF.
- **Parquet export (Arrow, gated):** write the per-receiver result table (id, x, y, z,
  received_power_dbm, path_loss_db, delay_spread_ns) to a Parquet file.
- **Build:** add `RFTRACE_ENABLE_GDAL` and `RFTRACE_ENABLE_PARQUET` (default OFF) mirroring the
  existing optional-dependency gating; JSON IO is always compiled. Gated APIs are always
  declared and throw a clear "built without GDAL/Parquet" error when the feature is absent, with
  `backendAvailable`-style availability helpers.

## Capabilities

### New Capabilities
- `georeferencing`: scene geographic origin + shared ENU projection + centroid-default.
- `geojson-import`: GeoJSON footprint → extruded building import.
- `cityjson-import`: CityJSON building objects → meshes.
- `osm-import`: Overpass JSON → buildings + vegetation.
- `antenna-msi-import`: MSI/PLN antenna pattern → `AntennaPattern` with H+V cuts.
- `geotiff-terrain`: GDAL DEM → terrain height field/mesh/sampler (gated).
- `czml-export`: results → Cesium CZML.
- `tiles3d-export`: scene/paths → 3D Tiles tileset + glTF.
- `geotiff-heatmap-export`: coverage → georeferenced GeoTIFF (gated).
- `parquet-export`: per-receiver result table → Parquet (Arrow, gated).

### Modified Capabilities
None. `AntennaPattern` gains a vertical cut, but the antenna capability is captured within the
new `antenna-msi-import` requirements for this change; existing living specs are untouched.

## Impact

- **Code (new):** `include/rftrace/importers/{geojson,cityjson,osm,msi,geotiff_terrain}.hpp`
  and `src/importers/*.cpp`; `include/rftrace/exporters/{czml,tiles3d,geotiff_heatmap,parquet}.hpp`
  and `src/exporters/*.cpp`; a footprint-extrusion helper; `Scene` georeference members and
  `AntennaPattern::verticalCutDb`. New IO lives under `src/importers/`, `src/exporters/`,
  `include/rftrace/{importers,exporters}/`, in the existing `io::` namespace.
- **Dependencies:** GDAL (`find_package(GDAL CONFIG)`) and Apache Arrow/Parquet
  (`find_package(Arrow CONFIG)` + `find_package(Parquet CONFIG)`) as optional, default-OFF
  Homebrew packages. nlohmann/json (already present) covers all always-on JSON IO.
- **Build guard:** the DEFAULT build (no GDAL/Parquet) MUST stay green — the current 125 C++
  tests are the primary regression guard. Gated tests are `#if RFTRACE_HAVE_*` no-ops otherwise.
- **Out of scope:** OSM PBF ingestion, hierarchical 3D-Tiles LOD, non-equirectangular / full
  geodetic projections, vector-tile or WMS services, reprojection between arbitrary CRS.
