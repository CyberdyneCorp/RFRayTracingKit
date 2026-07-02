# osm-pbf-import Specification

## Purpose
TBD - created by archiving change io-python-and-formats. Update Purpose after archive.
## Requirements
### Requirement: OSM PBF building and vegetation import
The library SHALL, when built with libosmium (`RFTRACE_ENABLE_OSMIUM`), import an OpenStreetMap
`.osm.pbf` file via `Scene::loadOSMPbf`, resolving node coordinates and projecting them through
the scene georeference, and SHALL apply the SAME building/vegetation extraction as the Overpass
JSON and OSM XML paths (buildings from a `building` tag with the documented height heuristics;
`natural=wood` / `landuse=forest` / `leisure=park` areas as vegetation).

#### Scenario: PBF building is extruded in an OSMIUM-enabled build
- **WHEN** a `.osm.pbf` file containing a way with a `building` tag is imported in an
  OSMIUM-enabled build
- **THEN** the scene SHALL gain a building mesh extruded using the same height heuristics as the
  Overpass JSON path

#### Scenario: PBF forest area becomes vegetation
- **WHEN** a `.osm.pbf` area tagged `landuse=forest` is imported in an OSMIUM-enabled build
- **THEN** the scene SHALL gain vegetation geometry covering that area

#### Scenario: PBF node coordinates are resolved and projected
- **WHEN** a `.osm.pbf` way references nodes defined in the file
- **THEN** the importer SHALL resolve those node lat/lon values and project them through the scene
  georeference

### Requirement: OSM PBF graceful degradation without osmium
The OSM PBF import API SHALL always be declared, and when the library is built without libosmium
it SHALL throw a clear "built without OSM PBF (osmium)" error and report unavailability via
`io::osmiumAvailable()`.

#### Scenario: Built without osmium throws clearly
- **WHEN** `Scene::loadOSMPbf` is called in a build compiled without libosmium
- **THEN** it SHALL throw an error stating the library was built without OSM PBF (osmium), and
  `io::osmiumAvailable()` SHALL return false

#### Scenario: Availability helper reflects the build
- **WHEN** `io::osmiumAvailable()` is queried
- **THEN** it SHALL return true only in an OSMIUM-enabled build and false otherwise

