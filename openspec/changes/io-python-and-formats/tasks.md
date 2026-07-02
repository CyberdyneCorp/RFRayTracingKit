> Expose the new geospatial IO + route/MIMO to Python, and add OSM XML (always-on) / OSM PBF
> (gated) import plus a hierarchical-LOD 3D-Tiles exporter at the C++ core. ADDITIVE only: the
> DEFAULT C++ build (no GDAL/Parquet/OSMIUM) must stay green (currently 207 tests) and existing
> Python tests must stay green (currently 46). Actually BUILD and RUN before checking off.

## 1. OSM XML import (`osm-import`)

- [x] 1.1 Add an in-tree `.osm` XML reader (no new always-on dependency): parse `<node id lat lon>`
      into a node table, `<way>` collecting `<nd ref/>` + `<tag k v/>`, resolve node coords
- [x] 1.2 Route XML ways through the SAME building/vegetation extraction as the Overpass-JSON path
      (building height heuristics; `natural=wood`/`landuse=forest`/`leisure=park` → vegetation)
- [x] 1.3 Make `Scene::loadOSM` autodetect `.osm` XML vs Overpass JSON by content/extension
- [x] 1.4 Tests: an XML building way extrudes to its tagged height; node refs resolve/project; a
      `landuse=forest` XML area becomes vegetation; autodetection routes both formats (default build)

## 2. OSM PBF import (`osm-pbf-import`, gated `RFTRACE_ENABLE_OSMIUM`)

- [x] 2.1 CMake: add `RFTRACE_ENABLE_OSMIUM` option (default OFF), `find_path` for
      `osmium`/`protozero`, link expat/zlib/bzip2/lz4, define `RFTRACE_HAVE_OSMIUM` (mirror GDAL/Parquet)
- [x] 2.2 `include/rftrace/importers/osm_pbf_importer.hpp` + gated `.cpp`: read `.osm.pbf` via
      header-only libosmium + protozero; `Scene::loadOSMPbf`
- [x] 2.3 Reuse the SAME building/vegetation extraction as the XML/Overpass paths
- [x] 2.4 Graceful degradation: `Scene::loadOSMPbf` always declared; throws
      "built without OSM PBF (osmium)" when built without osmium; add `io::osmiumAvailable()`
- [x] 2.5 Tests: `#if RFTRACE_HAVE_OSMIUM` PBF building/vegetation import (gated build); default
      build asserts the throw + `osmiumAvailable()==false` and stays green

## 3. Hierarchical-LOD 3D Tiles (`tiles3d-lod` + `tiles3d-export`)

- [x] 3.1 Add `exportPaths3DTilesLod(result, directory, maxDepth)` to the 3D-Tiles exporter;
      keep the single-tile `exportPaths3DTiles` unchanged
- [x] 3.2 Build a quadtree tile tree over the scene's horizontal extent to `maxDepth`; write a
      per-tile glb of the geometry within that tile's bounds
- [x] 3.3 Each tile: `boundingVolume` (box or region), `geometricError` decreasing with depth,
      documented `refine` ("ADD"/"REPLACE"); root `asset.version=="1.1"`, numeric root geometricError
- [x] 3.4 Tests: root has children (quadtree); every `content.uri` references a written `.glb`;
      child `geometricError` < parent; tileset re-parses as valid 3D Tiles 1.1 (default build)

## 4. Python IO bindings (`python-io-bindings`)

- [x] 4.1 Bind on the Python `Scene` wrapper: `load_geojson`, `load_cityjson`, `load_osm`,
      `set_geo_origin`, `geo_project` (mirror existing `toVec3`/`vec3ToArray`/`def_readwrite`)
- [x] 4.2 Bind top-level `rf.load_msi_antenna(path) -> AntennaPattern`
- [x] 4.3 Bind always-on exporters on `Result`/`CoverageResult` (or `rf.io`): `to_czml`, `to_3dtiles`
- [x] 4.4 Gated GDAL bindings under `#if RFTRACE_HAVE_GDAL`: `scene.load_terrain`,
      `coverage.to_geotiff`; gated Parquet binding under `#if RFTRACE_HAVE_PARQUET`: `result.to_parquet`
- [x] 4.5 Expose `rf.gdal_available()` / `rf.parquet_available()`; ensure gated bindings are absent
      or raise a clear error when the extension lacks the feature
- [x] 4.6 Build the Python extension WITH GDAL + Parquet ON so gated bindings exist and are tested
- [x] 4.7 Python tests: importers/exporters round-trip; availability helpers reflect the build;
      gated exporters exercised in the GDAL+Parquet extension

## 5. Python route + MIMO bindings (`python-route-mimo`)

- [x] 5.1 Bind `Route` (`waypoints`, `sample_spacing`, optional `speed`) and `RouteResult` /
      `RouteSample` (`position` + metrics + `doppler_hz`)
- [x] 5.2 Bind `Simulator.run_route(scene, route) -> RouteResult` (Python wrapper)
- [x] 5.3 Bind `rf.mimo.channel_matrix(receiver_result, tx_array, rx_array) -> numpy complex 2D`
      (shape `n_rx × n_tx`)
- [x] 5.4 Bind `rf.mimo.capacity(H, snr_linear)` and `rf.mimo.per_stream_sinr(H, snr_linear)`;
      bind `Result`/`io` `to_mimo_json`
- [x] 5.5 Python tests: `run_route` returns ordered samples with `doppler_hz`; `channel_matrix`
      shape/dtype match the C++ core; capacity + per-stream SINR match; `to_mimo_json` parses

## 6. Verification

- [x] 6.1 DEFAULT C++ build + ctest: 207 tests stay green (no GDAL/Parquet/OSMIUM)
- [x] 6.2 Gated C++ build + ctest with GDAL+Parquet+OSMIUM ON: all green, incl. new gated tests
- [x] 6.3 Python extension built WITH GDAL+Parquet ON; pytest: existing 46 + new tests green
- [x] 6.4 Update project docs / justfile recipes for the new formats and Python surface
