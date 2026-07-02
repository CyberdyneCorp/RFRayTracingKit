## ADDED Requirements

### Requirement: Scene geographic origin
The scene SHALL hold an optional geographic origin (`lat0`, `lon0`) exposed through
`Scene::setGeoOrigin(lat, lon)` and `Scene::hasGeoOrigin()`, and SHALL record it on the
`CoordinateSystem` (`georeferenced = true`, `originLat`, `originLon`) when set.

#### Scenario: Setting a geographic origin
- **WHEN** `setGeoOrigin(lat, lon)` is called on a scene
- **THEN** `hasGeoOrigin()` SHALL return true and the `CoordinateSystem` SHALL report
  `georeferenced = true` with `originLat`/`originLon` equal to the supplied values

#### Scenario: No origin by default
- **WHEN** a scene is default-constructed
- **THEN** `hasGeoOrigin()` SHALL return false and the `CoordinateSystem` SHALL report
  `georeferenced = false`

### Requirement: Equirectangular ENU projection
The scene SHALL provide `geoProject(lat, lon, alt)` returning a right-handed Z-up metric ENU
position using the equirectangular projection `x = (lon-lon0)*111320*cos(lat0)`,
`y = (lat-lat0)*110540`, `z = alt`, in double precision.

#### Scenario: Origin projects to the local origin
- **WHEN** `geoProject(lat0, lon0, 0)` is called
- **THEN** the result SHALL be `(0, 0, 0)` metres

#### Scenario: A displaced point projects to expected metres
- **WHEN** a point offset in longitude and latitude from the origin is projected
- **THEN** `x` and `y` SHALL equal the equirectangular formula values and `z` SHALL equal the
  supplied altitude

### Requirement: Centroid-default georeference for importers
When a geospatial importer runs on a scene that has no geographic origin, the importer SHALL set
the origin to the dataset's centroid before projecting so all imported data shares one frame.

#### Scenario: Importer sets origin from dataset centroid
- **WHEN** a geospatial dataset is imported into a scene with no prior geographic origin
- **THEN** the scene SHALL gain a geographic origin equal to the dataset's centroid and
  `hasGeoOrigin()` SHALL return true

#### Scenario: Existing origin is preserved
- **WHEN** a geospatial dataset is imported into a scene that already has a geographic origin
- **THEN** the importer SHALL keep the existing origin and project all data relative to it

### Requirement: Shared footprint extrusion
The core SHALL provide a shared helper that extrudes a polygon footprint (a ring of `Vec3` at a
base elevation plus a height) into wall triangles and a flat roof, reused by the GeoJSON,
CityJSON, and OSM importers.

#### Scenario: Footprint extrudes to walls and a roof
- **WHEN** a closed footprint ring with a base elevation and a positive height is extruded
- **THEN** the helper SHALL return wall triangles for every edge plus flat-roof triangles at
  `base + height`, forming a closed prism
