# geojson-import Specification

## Purpose
TBD - created by archiving change core-io-formats. Update Purpose after archive.
## Requirements
### Requirement: GeoJSON building footprint import
The library SHALL import building footprints from a GeoJSON `FeatureCollection` of `Polygon` and
`MultiPolygon` features, projecting each vertex through the scene georeference and extruding it to
a height taken from a feature property (`height`, else `levels` × storey height, else a documented
default) using the shared footprint-extrusion helper.

#### Scenario: Polygon footprint becomes an extruded building
- **WHEN** a GeoJSON `Polygon` feature with a `height` property is imported
- **THEN** the scene SHALL gain a building mesh whose footprint is the projected polygon and whose
  top is at that height above the base

#### Scenario: MultiPolygon yields multiple buildings
- **WHEN** a `MultiPolygon` feature is imported
- **THEN** each polygon SHALL be extruded into its own building geometry

### Requirement: GeoJSON georeference handling
GeoJSON import SHALL project coordinates through the scene georeference, setting the origin to the
dataset centroid when the scene has none.

#### Scenario: Centroid origin on first import
- **WHEN** GeoJSON is imported into a scene with no geographic origin
- **THEN** the scene SHALL adopt the dataset centroid as its geographic origin and project the
  footprints relative to it

### Requirement: GeoJSON error handling
GeoJSON import SHALL report a descriptive error and leave the scene unchanged when the input is
not valid GeoJSON or lacks usable polygon geometry.

#### Scenario: Invalid GeoJSON is rejected
- **WHEN** import is given a file that is not a valid `FeatureCollection` or has no polygon
  geometry
- **THEN** the library SHALL report a descriptive error and add no geometry to the scene

