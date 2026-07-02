## ADDED Requirements

### Requirement: GeoTIFF DEM terrain import
The library SHALL, when built with GDAL, import a GeoTIFF DEM into a terrain height field, a
triangulated terrain mesh whose posts are projected to local Z-up ENU metres through the scene
georeference, and an elevation sampler `elevationAt(x, y)` returning absolute metres.

#### Scenario: DEM produces heightfield, mesh, and sampler
- **WHEN** a GeoTIFF DEM is imported in a GDAL-enabled build
- **THEN** the result SHALL expose a height field, a triangulated terrain mesh in local ENU
  metres, and an `elevationAt(x, y)` sampler consistent with the raster values

#### Scenario: Elevation sampler matches raster posts
- **WHEN** `elevationAt` is queried at the local coordinates of a raster post
- **THEN** it SHALL return that post's DEM elevation in absolute metres

### Requirement: GeoTIFF terrain graceful degradation without GDAL
The GeoTIFF terrain import API SHALL always be declared, and when the library is built without
GDAL it SHALL throw a clear "built without GDAL" error and report unavailability via a
`gdalAvailable()`-style helper.

#### Scenario: Built without GDAL throws clearly
- **WHEN** DEM import is called in a build compiled without GDAL
- **THEN** it SHALL throw an error stating the library was built without GDAL, and
  `gdalAvailable()` SHALL return false

#### Scenario: Availability helper reflects the build
- **WHEN** `gdalAvailable()` is queried
- **THEN** it SHALL return true only in a GDAL-enabled build and false otherwise
