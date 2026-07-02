# geotiff-heatmap-export Specification

## Purpose
TBD - created by archiving change core-io-formats. Update Purpose after archive.
## Requirements
### Requirement: Coverage GeoTIFF heatmap export
The library SHALL, when built with GDAL, export a `CoverageResult` to a georeferenced single-band
GeoTIFF whose geotransform is derived from the grid origin and cell size plus the scene
georeference, using a no-data sentinel for uncovered cells.

#### Scenario: Coverage becomes a single-band GeoTIFF
- **WHEN** a `CoverageResult` is exported to GeoTIFF in a GDAL-enabled build
- **THEN** the output SHALL be a single-band raster whose pixel values are the selected coverage
  metric and whose geotransform matches the grid geometry and georeference

#### Scenario: Uncovered cells use the no-data sentinel
- **WHEN** the coverage grid contains cells with no signal
- **THEN** those pixels SHALL be written as the band's no-data value

### Requirement: GeoTIFF heatmap graceful degradation without GDAL
The heatmap export API SHALL always be declared, and when the library is built without GDAL it
SHALL throw a clear "built without GDAL" error and report unavailability via a
`gdalAvailable()`-style helper.

#### Scenario: Built without GDAL throws clearly
- **WHEN** heatmap export is called in a build compiled without GDAL
- **THEN** it SHALL throw an error stating the library was built without GDAL, and
  `gdalAvailable()` SHALL return false

