# gltf-export Specification

## Purpose
TBD - created by archiving change phase2-rf-multipath. Update Purpose after archive.
## Requirements
### Requirement: glTF ray-path export
The library SHALL export ray paths as glTF line geometry so paths can be inspected in
Blender/WebGL viewers.

#### Scenario: Paths export as line primitives
- **WHEN** a result is exported to glTF
- **THEN** the file SHALL contain line-primitive geometry with one polyline per path, whose
  vertices are the path points

#### Scenario: Paths are colored by power
- **WHEN** paths are exported to glTF with coloring enabled
- **THEN** each path's vertices SHALL carry a color attribute derived from the path's
  received power, so stronger and weaker paths are visually distinguishable

### Requirement: glTF receiver points
The library SHALL optionally include receiver positions as point geometry in the glTF
export.

#### Scenario: Receivers export as points
- **WHEN** receiver export is enabled
- **THEN** the glTF SHALL contain a point primitive with one vertex per receiver at its
  position

### Requirement: Valid glTF output
Exported glTF SHALL be a valid glTF 2.0 asset that loads without error in a conformant
importer.

#### Scenario: Output re-imports successfully
- **WHEN** the exported glTF is re-imported (e.g. via Assimp)
- **THEN** the import SHALL succeed and expose the exported geometry

