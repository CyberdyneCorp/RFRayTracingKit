# osm-import Specification

## Purpose
TBD - created by archiving change core-io-formats. Update Purpose after archive.
## Requirements
### Requirement: OSM Overpass JSON building import
The library SHALL import OpenStreetMap Overpass JSON, resolving node coordinates for each way and
projecting them through the scene georeference, and extrude ways/relations carrying a `building`
tag into building meshes.

#### Scenario: Building way is extruded
- **WHEN** an Overpass JSON way with a `building` tag and a `height` tag is imported
- **THEN** the scene SHALL gain a building mesh extruded to that tagged height

#### Scenario: Node coordinates are resolved and projected
- **WHEN** a building way references nodes defined elsewhere in the Overpass document
- **THEN** the importer SHALL resolve those node lat/lon values and project them through the
  scene georeference

### Requirement: OSM building height heuristics
When a building lacks an explicit `height` tag, the library SHALL derive the height from
`building:levels` multiplied by a documented storey height, and otherwise fall back to a
documented default height so buildings are never dropped.

#### Scenario: Levels-only building uses the fallback height
- **WHEN** a building way has `building:levels` but no `height`
- **THEN** the extruded height SHALL be `building:levels` × the documented storey height

#### Scenario: Untagged-height building uses the default
- **WHEN** a building way has neither `height` nor `building:levels`
- **THEN** the library SHALL extrude it to the documented default height rather than skip it

### Requirement: OSM vegetation import
The library SHALL import areas tagged `natural=wood`, `landuse=forest`, or `leisure=park` as
vegetation geometry in the scene.

#### Scenario: Forest polygon becomes vegetation
- **WHEN** an Overpass JSON area tagged `landuse=forest` is imported
- **THEN** the scene SHALL gain vegetation geometry covering that area

