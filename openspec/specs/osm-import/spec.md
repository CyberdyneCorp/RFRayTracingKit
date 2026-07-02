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

### Requirement: OSM XML (.osm) building and vegetation import
The library SHALL import the OpenStreetMap `.osm` XML format with a lightweight in-tree reader
(adding no new always-on dependency): `<node id lat lon>` elements build a node coordinate table,
`<way>` elements collect their `<nd ref/>` node references and `<tag k v/>` tags, node coordinates
are resolved and projected through the scene georeference, and the SAME building/vegetation
extraction as the Overpass-JSON path is applied (buildings from a `building` tag with the
documented height heuristics; `natural=wood` / `landuse=forest` / `leisure=park` areas as
vegetation).

#### Scenario: OSM XML building way is extruded
- **WHEN** an `.osm` XML `<way>` with a `building` tag and a `height` tag is imported
- **THEN** the scene SHALL gain a building mesh extruded to that tagged height, identical to the
  Overpass-JSON path

#### Scenario: OSM XML node references are resolved
- **WHEN** an `.osm` XML `<way>` references `<node>` elements defined earlier in the document
- **THEN** the reader SHALL resolve those node lat/lon values and project them through the scene
  georeference

#### Scenario: OSM XML forest area becomes vegetation
- **WHEN** an `.osm` XML area tagged `landuse=forest` is imported
- **THEN** the scene SHALL gain vegetation geometry covering that area

### Requirement: OSM format autodetection
`Scene::loadOSM` SHALL autodetect whether the input is `.osm` XML or Overpass JSON (by content
and/or extension) and route it to the corresponding reader, so a single entry point handles both
always-on OSM formats.

#### Scenario: XML input is routed to the XML reader
- **WHEN** `Scene::loadOSM` is called on an `.osm` XML document
- **THEN** it SHALL parse it with the XML reader and produce the same building/vegetation geometry
  the extraction defines

#### Scenario: Overpass JSON input still parses
- **WHEN** `Scene::loadOSM` is called on an Overpass JSON document
- **THEN** it SHALL parse it via the existing Overpass-JSON path, unchanged

