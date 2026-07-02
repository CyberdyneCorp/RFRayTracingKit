## ADDED Requirements

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
