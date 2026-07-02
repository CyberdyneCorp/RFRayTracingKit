## ADDED Requirements

### Requirement: CityJSON building import
The library SHALL import CityJSON `Building` and `BuildingPart` city objects into scene meshes,
using solid/surface boundaries where present and otherwise extruding the footprint to the object
height via the shared footprint-extrusion helper.

#### Scenario: CityJSON building becomes a mesh
- **WHEN** a CityJSON document containing one `Building` is imported
- **THEN** the scene SHALL gain a triangulated mesh representing that building

#### Scenario: Footprint fallback when no solid geometry
- **WHEN** a CityJSON building has only a footprint plus a height attribute
- **THEN** the library SHALL extrude the footprint to that height using the shared extrusion helper

### Requirement: CityJSON transform and georeference
CityJSON import SHALL apply the document's `transform` (scale and translate) to its integer
vertices and then project the resulting coordinates through the scene georeference (centroid
default when unset).

#### Scenario: Transform is applied before projection
- **WHEN** a CityJSON document with a `transform` block is imported
- **THEN** vertices SHALL be dequantized with the transform's scale/translate before being
  projected through the georeference

### Requirement: CityJSON error handling
CityJSON import SHALL report a descriptive error and leave the scene unchanged when the input is
not a valid CityJSON document.

#### Scenario: Invalid CityJSON is rejected
- **WHEN** import is given a file whose `type` is not `CityJSON` or that lacks `vertices`
- **THEN** the library SHALL report a descriptive error and add no geometry to the scene
