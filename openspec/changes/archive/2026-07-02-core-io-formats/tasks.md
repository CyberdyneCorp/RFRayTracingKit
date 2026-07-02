> Geospatial IO promoted from Python examples into the C++ core. JSON IO is always built;
> GDAL and Parquet are default-OFF, flag-gated, and graceful when absent. The DEFAULT build's
> 125 C++ tests are the primary regression guard and must stay green.

## 1. Georeferencing + shared extrusion (`georeferencing`)

- [x] 1.1 Extend `CoordinateSystem` with `originLat`/`originLon` (and keep `georeferenced`); add
      `Scene::setGeoOrigin(lat,lon)`, `Scene::hasGeoOrigin()`, `Scene::geoProject(lat,lon,alt)`
- [x] 1.2 Implement the D1 equirectangular ENU projection (Z-up metres, double precision)
- [x] 1.3 Centroid-default: geospatial importers set the origin to the dataset centroid when unset
      (shared `detail::ensureGeoOrigin` in `src/importers/geo_import_util.cpp`; verified by
      `GeoJsonImport.CentroidBecomesOriginWhenUnset`)
- [x] 1.4 D2 footprint-extrusion helper (ring + base + height → wall + flat-roof triangles)
- [x] 1.5 Tests: projection maps origin→(0,0), known lat/lon→expected metres, round-trips via the
      CZML inverse; extrusion produces closed walls + roof with correct triangle count/winding
      (projection + extrusion tests done in `tests/test_georef.cpp`; CZML inverse round-trip pending §6)

## 2. GeoJSON import (`geojson-import`)

- [x] 2.1 `importers/geojson_importer.hpp` + `.cpp`: parse Polygon/MultiPolygon footprints,
      height/levels property, project via D1, extrude via D2 into building meshes; Point features
      become receivers/transmitters. `Scene::loadGeoJSON` added.
- [x] 2.2 Set/consume the scene georeference (centroid default when unset)
- [x] 2.3 Tests: a footprint FeatureCollection yields extruded buildings at the expected local
      coordinates; missing/invalid geometry errors clearly (`tests/test_geo_import.cpp`)

## 3. CityJSON import (`cityjson-import`)

- [x] 3.1 `importers/cityjson_importer.hpp` + `.cpp`: parse CityJSON vertices +
      `Building`/`BuildingPart`; triangulate solid/surface boundaries where present, else
      footprint+height extrusion (D2). `Scene::loadCityJSON` added.
- [x] 3.2 Apply CityJSON transform (scale/translate) then project via D1
- [x] 3.3 Tests: a minimal CityJSON building box yields a 12-triangle mesh; transform applied
      correctly (`CityJsonImport.BuildingBoxWithTransform`)

## 4. OSM import (`osm-import`)

- [x] 4.1 `importers/osm_importer.hpp` + `.cpp`: parse Overpass JSON, resolve node coords, project
      via D1. `Scene::loadOSM` added.
- [x] 4.2 `building` ways → extruded buildings (height from `height`, else `building:levels`×3 m
      storey, else 6 m default); `natural=wood`/`landuse=forest`/`leisure=park` → vegetation (8 m)
- [x] 4.3 Tests: a building way is extruded to its tagged height; a levels-only way uses the
      levels×storey fallback; a `natural=wood` polygon becomes vegetation
      (`OsmImport.BuildingsVegetationAndHeightHeuristics`)

## 5. MSI antenna import (`antenna-msi-import`)

- [x] 5.1 Extend `AntennaPattern` with `verticalCutDb`; `gainTowards` combines H+V cuts (D5)
      (`gainTowards` now returns peak + H(azimuth 0–180°) + V(elevation 0–90°); empty cut = 0 dB,
      so omni/azimuth-only behaviour is unchanged — verified by `MsiImport.EmptyVerticalCutIsBackwardCompatible`)
- [x] 5.2 `importers/msi_importer.hpp` + `.cpp`: parse NAME/FREQUENCY/GAIN/HORIZONTAL/VERTICAL
      tables into an `AntennaPattern`; GAIN normalized to dBi (`dBd` + 2.15). Free function
      `io::loadMsiAntenna(path)`
- [x] 5.3 Tests (`tests/test_msi_antenna.cpp`): peak gain at boresight, horizontal + vertical +
      combined roll-off match the tables, dBi gain not converted, and missing-file / no-table /
      truncated-table all error clearly

## 6. CZML export (`czml-export`)

- [x] 6.1 `exporters/czml_exporter.hpp` + `.cpp`: document packet + receiver point packets +
      ray-path polyline packets; `cartographicDegrees` when georeferenced (inverse D1), else
      `cartesian` (scene-aware overload carries the georef)
- [x] 6.2 Tests (`tests/test_czml.cpp`): output parses as JSON with a `document` packet; a
      georeferenced scene emits cartographicDegrees (inverse-D1 values checked); a
      non-georeferenced scene emits cartesian; packet count = 1 + receivers + paths

## 7. 3D Tiles export (`tiles3d-export`)

- [x] 7.1 `exporters/tiles3d_exporter.hpp` + `.cpp`: emit valid 3D Tiles 1.1 `tileset.json` (asset,
      geometricError, root tile + bounding volume + content.uri) referencing a single-tile
      `content.glb` built by wrapping the existing `gltf_exporter` output (data-URI buffer decoded
      into a real GLB BIN chunk so GLB readers such as Assimp accept it)
- [x] 7.2 Tests (`tests/test_tiles3d.cpp`): tileset.json parses, has `asset.version` "1.1", numeric
      geometricError, a root tile with bounding volume + content.uri, the referenced `content.glb`
      is written, and it re-imports via Assimp

## 8. GeoTIFF terrain import — GDAL, gated (`geotiff-terrain`)

- [x] 8.1 CMake: `option(RFTRACE_ENABLE_GDAL OFF)` + `find_package(GDAL CONFIG)` +
      `RFTRACE_HAVE_GDAL`; the GeoTIFF `.cpp` is always compiled with its GDAL body guarded by
      `RFTRACE_HAVE_GDAL` (GDAL linked/defined only when ON), so the entry points always resolve
- [x] 8.2 `importers/geotiff_terrain.hpp` (always declared): DEM → terrain height field +
      triangulated mesh (posts projected via D1) + `elevationAt(x,y)` sampler; `Scene::loadTerrain`
      adds 'soil' terrain, defaults the georeference to the DEM centroid, and (opt-in) offsets
      subsequently-imported building bases via `detail::footprintBaseZ` (GeoJSON/OSM)
- [x] 8.3 Graceful degradation: when built without GDAL `loadGeoTiffDem`/`Scene::loadTerrain`
      throw "built without GDAL" and `gdalAvailable()` returns false
- [x] 8.4 Tests (`tests/test_geotiff.cpp`): `#if RFTRACE_HAVE_GDAL` author a synthetic DEM, load
      it, check raster/geotransform/sampler/mesh + centroid origin; `#else` assert the throw +
      `gdalAvailable()==false` (the no-op `GeoTiff.GracefulWithoutGdal` in the default build)

## 9. GeoTIFF heatmap export — GDAL, gated (`geotiff-heatmap-export`)

- [x] 9.1 `exporters/geotiff_heatmap.hpp` (always declared): `CoverageResult` → georeferenced
      single-band Float32 GeoTIFF (geotransform from grid origin/cellSize + scene georeference,
      vertical flip so raster row 0 is the northern edge, `-9999` no-data for uncovered cells);
      metric-selectable (power/path-loss/SINR) with a scene-aware overload
- [x] 9.2 Graceful degradation: `exportCoverageGeoTiff` throws "built without GDAL" when absent;
      `gdalAvailable()`
- [x] 9.3 Tests (`tests/test_geotiff.cpp`): `#if RFTRACE_HAVE_GDAL` export+reopen a GeoTIFF and
      check band count/geotransform/values + no-data; `#else` assert the throw (no-op in the
      default build)

## 10. Parquet export — Arrow, gated (`parquet-export`)

- [x] 10.1 CMake: `option(RFTRACE_ENABLE_PARQUET OFF)` + `find_package(Arrow CONFIG)` +
      `find_package(Parquet CONFIG)` + `RFTRACE_HAVE_PARQUET`; compile gated `.cpp` only when ON
      (option + gating wired; no gated `.cpp` yet)
- [x] 10.2 `exporters/parquet_exporter.hpp` (always declared) + `.cpp` (always compiled, Arrow body
      guarded by `RFTRACE_HAVE_PARQUET`): per-receiver table (id, x, y, z, received_power_dbm,
      path_loss_db, delay_spread_ns) → Parquet via Arrow (`io::exportReceiversParquet`)
- [x] 10.3 Graceful degradation: `exportReceiversParquet` throws "built without Parquet" when absent;
      `parquetAvailable()`
- [x] 10.4 Tests (`tests/test_parquet.cpp`): `#if RFTRACE_HAVE_PARQUET` write+reopen a Parquet file
      and check schema/rows/columns; `#else` assert the throw + `parquetAvailable()==false` (the
      no-op `Parquet.GracefulWithoutParquet` in the default build)

## 11. Integration, build & validation

- [x] 11.1 Register new always-on sources in the library + tests CMake (JSON importers/exporters)
- [x] 11.2 DEFAULT build green: `cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew` +
      build + `ctest` still passes (125 baseline + the new always-on tests), no GDAL/Parquet needed
- [x] 11.3 GATED build green: `-DRFTRACE_ENABLE_GDAL=ON -DRFTRACE_ENABLE_PARQUET=ON` builds and
      the gated tests pass
- [x] 11.4 Examples/docs: document the georeference/projection, OSM/GeoJSON/CityJSON/MSI import,
      and CZML/3D-Tiles/GeoTIFF/Parquet export; note the GDAL/Parquet flags
- [x] 11.5 `openspec validate core-io-formats --strict` passes
