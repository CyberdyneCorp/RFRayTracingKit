## Context

Phases 1–7 plus the `core-io-formats` change give a validated, backend-agnostic, Python-free
C++ core: `Scene` with a georeference (`setGeoOrigin`/`hasGeoOrigin`/`geoProject`) and geospatial
importers (`loadGeoJSON`/`loadCityJSON`/`loadOSM`/`loadTerrain`, `io::loadMsiAntenna`), exporters
(`czml`/`tiles3d`/`geotiff_heatmap`/`parquet`), route/drive-test simulation (`Route`,
`RouteSample`, `RouteResult`, `Simulator::runRoute`), and narrowband MIMO (`rf::channelMatrix`,
`rf::capacity`, `rf::perStreamSinr`, `io::mimoToJsonString`). The pybind11 module
(`bindings/python/pybind11_module.cpp`) binds only the older scene/simulation/result surface;
none of the new IO or route/MIMO is exposed. OSM ingestion accepts only Overpass JSON, and the
3D-Tiles exporter emits a single tile.

The core stays right-handed **Z-up metres, double precision, Python-free**. Python lives only
under `bindings/python/`. New C++ IO lives under `src/importers`, `src/exporters`,
`include/rftrace/{importers,exporters}`. GDAL, Arrow/Parquet, and now OSMIUM are optional,
default-OFF, and must not perturb the default build (the primary regression guard: 207 C++ tests
plus 46 Python tests stay green).

## Goals / Non-Goals

**Goals:**
- Expose the new geospatial IO + route + MIMO to Python behind the existing module conventions
  (`toVec3`/`vec3ToArray`, `def_readwrite`, `materials`/`io` submodules, NumPy interop).
- Return NumPy arrays for the MIMO channel matrix `H`.
- GDAL/Parquet-gated Python bindings that are present + tested when the extension is built with
  those deps, and absent-or-clearly-erroring with `rf.gdal_available()`/`rf.parquet_available()`
  otherwise.
- Always-on OSM `.osm` XML import with an in-tree reader (no new always-on dependency), reusing
  the existing building/vegetation extraction.
- Gated OSM `.osm.pbf` import via header-only libosmium/protozero, graceful when absent.
- A hierarchical-LOD (quadtree) 3D-Tiles 1.1 exporter that keeps the single-tile function.

**Non-Goals:**
- Non-equirectangular / full geodetic projection, arbitrary CRS reprojection, OSM/PBF writing,
  implicit 3D-Tiles tiling / subtree binaries, batched content merging, Python packaging/wheel
  changes, GPU involvement.

## Decisions

### D1. Python IO bindings mirror the existing module patterns
Bind on the Python `Scene` wrapper: `load_geojson`, `load_cityjson`, `load_osm`, `set_geo_origin`,
`geo_project`, and (GDAL-gated) `load_terrain`; a top-level `rf.load_msi_antenna(path) ->
AntennaPattern`; and exporters on the `Result` / `CoverageResult` wrappers (or `rf.io`):
`to_czml`, `to_3dtiles`, coverage `to_geotiff` (GDAL), receivers `to_parquet` (Arrow). The gated
bindings are compiled under `#if RFTRACE_HAVE_GDAL` / `#if RFTRACE_HAVE_PARQUET`; when the
extension was built without a feature, the corresponding binding is either absent or raises a
clear error, and `rf.gdal_available()` / `rf.parquet_available()` report the build. The Python
extension is built WITH GDAL + Parquet ON so the gated bindings exist and are exercised by the
Python test suite. Rationale: matches the established `toVec3`/`vec3ToArray`, `def_readwrite`,
`materials`/`io` submodule conventions and the C++ `RFTRACE_HAVE_*` gating, so the Python surface
tracks the core one-to-one.

### D2. Python route + MIMO bindings; NumPy for H
Bind `Route` (`waypoints`, `sample_spacing`, optional `speed`), `RouteResult` (an ordered
`RouteSample` series carrying `position` + RF metrics + `doppler_hz`), and
`Simulator.run_route(scene, route) -> RouteResult` (a Python wrapper). Bind MIMO under
`rf.mimo`: `channel_matrix(receiver_result, tx_array, rx_array)` returns a NumPy **complex 2D**
array (`n_rx × n_tx`), `capacity(H, snr_linear)` returns bits/s/Hz, and
`per_stream_sinr(H, snr_linear)` returns the descending per-stream SINRs; plus `Result`/`io`
`to_mimo_json`. Rationale: NumPy complex arrays are the idiomatic Python container for `H` and
interoperate with `numpy.linalg`; the route wrapper mirrors the existing `Result`/`CoverageResult`
wrapper style.

### D3. OSM `.osm` XML import (always-on, in-tree reader)
Parse the OSM XML format with a lightweight in-tree reader — no new always-on dependency:
`<node id lat lon>` builds a node coordinate table; `<way>` collects `<nd ref/>` node references
and `<tag k v/>` tags; node coordinates are resolved and projected via the scene georeference.
Building/vegetation extraction is the SAME as the Overpass-JSON path: ways with a `building` tag
are extruded (height from `height`, else `building:levels` × storey height, else the default
height); `natural=wood` / `landuse=forest` / `leisure=park` areas become vegetation.
`Scene::loadOSM` autodetects `.osm` XML vs Overpass JSON by content/extension. Rationale: `.osm`
XML is the common OSM export; reusing the extraction keeps a single definition of building/
vegetation semantics; an in-tree reader avoids adding an always-on XML dependency.

### D4. OSM `.osm.pbf` import (gated `RFTRACE_ENABLE_OSMIUM`)
Read `.osm.pbf` via header-only libosmium + protozero, linking expat/zlib/bzip2/lz4, using the
SAME building/vegetation extraction. `Scene::loadOSMPbf` is ALWAYS declared; built without osmium
it throws "built without OSM PBF (osmium)", and `io::osmiumAvailable()` reports the build. CMake
adds an `RFTRACE_ENABLE_OSMIUM` option, `find_path` for `osmium`/`protozero`, links the
compression libraries, and defines `RFTRACE_HAVE_OSMIUM` — mirroring the GDAL/Parquet gating.
Rationale: PBF is the compact distribution format; libosmium is header-only so it adds no
always-on cost; graceful degradation matches the established gated-IO contract.

### D5. Hierarchical-LOD 3D-Tiles (quadtree tile tree)
Add `exportPaths3DTilesLod(result, dir, maxDepth)` that emits a TILE TREE over the scene's
horizontal extent: a root tile plus recursively subdivided child tiles (a quadtree in X/Y down
to `maxDepth`). Each tile has its OWN glb content holding the geometry/paths within that tile's
bounds, a `boundingVolume` (box or region), and a `geometricError` that decreases with depth. The
`refine` mode ("ADD" or "REPLACE") is documented. The output is valid 3D Tiles 1.1: a caller
loading `tileset.json` gets a valid hierarchy whose `content.uri`s reference `.glb` files that
are written. The existing single-tile `exportPaths3DTiles` is retained unchanged. Rationale: a
tile tree enables view-dependent streaming for city-scale scenes; keeping the single-tile
function preserves the existing `tiles3d-export` contract and tests.

## Risks / Trade-offs

- **Gated Python bindings drift** (extension may be built with or without GDAL/Parquet) →
  compile the bindings under `#if RFTRACE_HAVE_*`, expose `gdal_available()`/`parquet_available()`,
  and build the tested extension WITH both ON so the gated paths are exercised.
- **OSM XML dialect variance** (`.osm` vs Overpass JSON, tag styles) → autodetect by
  content/extension and route both to one shared extraction; error clearly on unrecognized input.
- **New gated dependency (osmium)** could perturb the default build → `RFTRACE_ENABLE_OSMIUM`
  default OFF, code compile-excluded, gated tests `#if RFTRACE_HAVE_OSMIUM` no-ops; the 207-test
  default build is the guard.
- **3D-Tiles LOD explosion** (deep quadtrees write many glb files) → bound depth by `maxDepth`
  and let empty tiles carry no content; document `refine` semantics so viewers refine correctly.
- **Python↔C++ array copies** for `H` → returning a NumPy complex array is a copy, acceptable for
  the small `n_rx × n_tx` matrices MIMO produces.

## Migration Plan

Purely additive over Phases 1–7 and `core-io-formats`; no breaking API changes. Recommended
implementation order:
1. OSM `.osm` XML import + `loadOSM` autodetection (always-on) with regression tests.
2. Gated OSM PBF import (`RFTRACE_ENABLE_OSMIUM`) with `#if RFTRACE_HAVE_OSMIUM` tests.
3. Hierarchical-LOD 3D-Tiles exporter (`exportPaths3DTilesLod`) with tileset-validity tests.
4. Python IO bindings (D1) + route/MIMO bindings (D2), building the extension WITH GDAL+Parquet.
5. Python tests for every bound surface, including gated availability behavior.

GDAL/Parquet/OSMIUM stay OFF by default so existing C++ (207) and Python (46) tests are
unaffected.

## Resolved Decisions

All five decisions D1–D5 above are resolved and to be implemented as written; there are no open
questions.
- **Python IO (D1):** Scene loaders + `rf.load_msi_antenna` + Result/CoverageResult exporters;
  GDAL/Parquet gated with `gdal_available()`/`parquet_available()`; extension built with both ON.
- **Python route/MIMO (D2):** `Route`/`RouteResult`/`run_route`; `rf.mimo.*` with NumPy complex
  `H`; `to_mimo_json`.
- **OSM XML (D3):** always-on in-tree reader, shared extraction, `loadOSM` autodetection.
- **OSM PBF (D4):** gated libosmium/protozero, `loadOSMPbf`, `osmiumAvailable()`, graceful when
  absent.
- **3D-Tiles LOD (D5):** quadtree tile tree with per-tile glb + decreasing `geometricError`,
  valid 3D Tiles 1.1, single-tile function retained.
