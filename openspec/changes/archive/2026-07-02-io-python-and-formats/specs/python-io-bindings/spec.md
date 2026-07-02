## ADDED Requirements

### Requirement: Python geospatial scene import bindings
The Python `Scene` wrapper SHALL expose the always-on geospatial importers `load_geojson`,
`load_cityjson`, and `load_osm`, plus the georeference operations `set_geo_origin(lat, lon)` and
`geo_project(lat, lon, alt)`, each delegating to the corresponding C++ core method.

#### Scenario: GeoJSON footprints imported from Python
- **WHEN** `scene.load_geojson(path)` is called from Python on a footprint FeatureCollection
- **THEN** the scene SHALL gain the extruded building geometry produced by the C++ `loadGeoJSON`

#### Scenario: Geographic origin set and projection queried from Python
- **WHEN** `scene.set_geo_origin(lat, lon)` is called and then `scene.geo_project(lat, lon, 0)`
- **THEN** `geo_project` at the origin SHALL return the local origin `(0, 0, 0)` in Z-up metres

#### Scenario: OSM imported from Python
- **WHEN** `scene.load_osm(path)` is called from Python on a supported OSM document
- **THEN** the scene SHALL gain the building/vegetation geometry produced by the C++ `loadOSM`

### Requirement: Python MSI antenna import binding
The Python module SHALL expose a top-level `rf.load_msi_antenna(path)` returning an
`AntennaPattern` with the horizontal and vertical cuts parsed by the C++ `io::loadMsiAntenna`.

#### Scenario: MSI file loaded into an AntennaPattern
- **WHEN** `rf.load_msi_antenna(path)` is called on a valid `.msi`/`.pln` file
- **THEN** it SHALL return an `AntennaPattern` whose gain toward boresight matches the file's peak
  gain

### Requirement: Python always-on exporter bindings
The Python `Result` / `CoverageResult` wrappers (or `rf.io`) SHALL expose the always-on
exporters `to_czml` and `to_3dtiles`, delegating to the corresponding C++ core exporters.

#### Scenario: Result exported to CZML from Python
- **WHEN** `result.to_czml(path)` is called from Python
- **THEN** a CZML document SHALL be written equivalent to the C++ CZML exporter's output

#### Scenario: Result exported to 3D Tiles from Python
- **WHEN** `result.to_3dtiles(directory)` is called from Python
- **THEN** a `tileset.json` and its referenced glb SHALL be written by the C++ 3D-Tiles exporter

### Requirement: Python gated GDAL exporter and terrain bindings
When the extension is built with GDAL, the Python `Scene` wrapper SHALL expose `load_terrain` and
the `CoverageResult` wrapper SHALL expose `to_geotiff`, delegating to the GDAL-backed C++ core.

#### Scenario: Coverage exported to GeoTIFF in a GDAL-enabled extension
- **WHEN** the extension is built with GDAL and `coverage.to_geotiff(path)` is called
- **THEN** a georeferenced single-band GeoTIFF of the coverage metric SHALL be written

#### Scenario: Terrain loaded from a DEM in a GDAL-enabled extension
- **WHEN** the extension is built with GDAL and `scene.load_terrain(dem_path)` is called
- **THEN** the scene SHALL gain the terrain produced by the C++ `loadTerrain`

### Requirement: Python gated Parquet exporter binding
When the extension is built with Arrow/Parquet, the Python `Result` wrapper (or `rf.io`) SHALL
expose `to_parquet` writing the per-receiver result table via the C++ Parquet exporter.

#### Scenario: Receivers exported to Parquet in a Parquet-enabled extension
- **WHEN** the extension is built with Parquet and `result.to_parquet(path)` is called
- **THEN** a Parquet file of the per-receiver table SHALL be written

### Requirement: Python gated-IO availability and graceful degradation
The Python module SHALL expose `rf.gdal_available()` and `rf.parquet_available()` reporting the
extension's build, and any GDAL/Parquet-gated binding invoked in an extension built without that
feature SHALL either be absent or raise a clear error stating the missing feature.

#### Scenario: Availability helpers reflect the build
- **WHEN** `rf.gdal_available()` and `rf.parquet_available()` are queried
- **THEN** each SHALL return `True` only when the extension was built with that feature and
  `False` otherwise

#### Scenario: Gated exporter without the feature degrades clearly
- **WHEN** a GDAL/Parquet-gated binding is invoked in an extension built without that feature
- **THEN** it SHALL either be absent from the module or raise a clear error naming the missing
  feature, without crashing the interpreter
