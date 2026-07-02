## Why

The C++ core now has first-class geospatial IO (GeoJSON / CityJSON / OSM Overpass JSON /
MSI antenna import; CZML / 3D-Tiles / GeoTIFF / Parquet export) and advanced RF (route /
drive-test simulation and narrowband MIMO). None of that is reachable from Python: the
pybind11 module still only binds the older scene/simulation/result surface, so Python users
cannot load a georeferenced scene from open data, export to Cesium/QGIS/analytics, run a
drive-test route, or build a MIMO channel matrix. Python is where most planning and analysis
happens, so these capabilities are effectively unavailable to the primary audience.

Two format gaps also remain at the core. First, OSM ingestion only accepts Overpass JSON;
the far more common `.osm` XML export and the compact `.osm.pbf` binary format cannot be
loaded, forcing users through an external conversion step. Second, the 3D-Tiles exporter
emits a single tile with no level-of-detail, so large scenes load as one monolithic glb with
no view-dependent refinement — unusable for city-scale streaming in CesiumJS.

This change (1) exposes the new IO + route/MIMO to Python behind the existing module patterns,
and (2) adds OSM XML (always-on) and OSM PBF (gated) import plus a hierarchical-LOD 3D-Tiles
exporter at the C++ core. It is purely additive: the Python-free core stays Python-free, the
default C++ build (no GDAL/Parquet/OSMIUM) stays green, and existing Python tests stay green.

## What Changes

- **Python IO bindings (`python-io-bindings`):** expose on the Python `Scene` wrapper
  `load_geojson`, `load_cityjson`, `load_osm`, `set_geo_origin`, `geo_project`, and (GDAL-gated)
  `load_terrain`; a top-level `rf.load_msi_antenna(path) -> AntennaPattern`; and exporters on
  the `Result` / `CoverageResult` wrappers (or `rf.io`): `to_czml`, `to_3dtiles`, coverage
  `to_geotiff` (GDAL), receivers `to_parquet` (Arrow). GDAL/Parquet bindings are compiled under
  `#if RFTRACE_HAVE_*`; when the extension was built without them the binding is absent or
  raises a clear error, and `rf.gdal_available()` / `rf.parquet_available()` report the build.
- **Python route + MIMO bindings (`python-route-mimo`):** bind `Route` (waypoints, sample
  spacing, optional speed), `RouteResult` (ordered `RouteSample` series with position + metrics
  + `doppler_hz`), and `Simulator.run_route(scene, route) -> RouteResult`; and MIMO:
  `rf.mimo.channel_matrix(receiver_result, tx_array, rx_array) -> numpy complex 2D`,
  `rf.mimo.capacity(H, snr_linear)`, `rf.mimo.per_stream_sinr(H, snr_linear)`, plus a
  `Result`/`io` `to_mimo_json`. `H` is returned as a NumPy complex array.
- **OSM XML import (`osm-import`, always-on):** parse the `.osm` XML format with a lightweight
  in-tree reader (no new always-on dependency): `<node id lat lon>`, `<way><nd ref/><tag k v/>`,
  resolve node coordinates, and reuse the SAME building/vegetation extraction as the
  Overpass-JSON path. `Scene::loadOSM` autodetects `.osm` XML vs Overpass JSON.
- **OSM PBF import (`osm-pbf-import`, gated `RFTRACE_ENABLE_OSMIUM`):** read `.osm.pbf` via
  header-only libosmium + protozero, linking expat/zlib/bzip2/lz4, using the SAME extraction.
  `Scene::loadOSMPbf`; built without osmium it throws "built without OSM PBF (osmium)"; expose
  `io::osmiumAvailable()`.
- **Hierarchical-LOD 3D Tiles (`tiles3d-lod`):** add `exportPaths3DTilesLod(result, dir,
  maxDepth)` emitting a quadtree tile TREE over the scene's horizontal extent — a root tile plus
  recursively subdivided child tiles, each with its own glb content, a bounding volume, and a
  `geometricError` that decreases with depth; valid 3D Tiles 1.1. The existing single-tile
  `exportPaths3DTiles` (`tiles3d-export`) is retained unchanged.
- **Build:** add `RFTRACE_ENABLE_OSMIUM` (default OFF → `RFTRACE_HAVE_OSMIUM`) mirroring the
  existing GDAL/Parquet gating; build the Python extension WITH GDAL + Parquet ON so the gated
  bindings exist and are tested.

## Capabilities

### New Capabilities
- `python-io-bindings`: Python access to the geospatial importers + exporters (GDAL/Parquet
  gated) with availability helpers.
- `python-route-mimo`: Python access to route/drive-test simulation and narrowband MIMO,
  returning NumPy arrays.
- `osm-pbf-import`: gated `.osm.pbf` import via libosmium/protozero, graceful when absent.
- `tiles3d-lod`: hierarchical-LOD (quadtree) 3D-Tiles 1.1 tileset export.

### Modified Capabilities
- `osm-import`: ADD OSM `.osm` XML import (always-on) and format autodetection alongside the
  existing Overpass-JSON path.
- `tiles3d-export`: ADD the guarantee that the single-tile exporter is retained and that a
  separate hierarchical-LOD variant is offered (owned by `tiles3d-lod`).

## Impact

- **Code (new):** `include/rftrace/importers/osm_pbf_importer.hpp` + `src/importers/*.cpp` (PBF,
  gated) and OSM XML parsing added to the existing `osm_importer` unit; a LOD variant added to
  `include/rftrace/exporters/tiles3d_exporter.hpp` + `src/exporters/tiles3d_exporter.cpp`; new
  pybind11 bindings in `bindings/python/pybind11_module.cpp` and Python wrappers under
  `bindings/python/rftracekit/`.
- **Dependencies:** libosmium 2.23 + protozero 1.8 (header-only) with expat/zlib/bzip2/lz4 for
  the gated PBF path; pybind11 (already present) for bindings. No new always-on dependency
  (OSM XML uses an in-tree reader).
- **Build guard:** the DEFAULT C++ build (no GDAL/Parquet/OSMIUM) MUST stay green (currently
  207 tests) and existing Python tests MUST stay green (currently 46). Gated code is
  compile-excluded and gated tests are `#if RFTRACE_HAVE_*` no-ops otherwise.
- **Out of scope:** non-equirectangular / full geodetic projection, writing OSM/PBF, batched
  tileset content merging, implicit 3D-Tiles tiling / subtree files, arbitrary CRS reprojection,
  Python packaging/wheels changes.
