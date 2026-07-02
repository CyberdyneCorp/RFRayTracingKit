# scene-import Specification

## Purpose
TBD - created by archiving change phase1-cpu-prototype. Update Purpose after archive.
## Requirements
### Requirement: Triangle mesh import
The library SHALL import triangle-mesh geometry from glTF (`.gltf`/`.glb`) and OBJ files
into the scene model, triangulating polygonal faces as needed.

#### Scenario: Load a glTF building model
- **WHEN** `loadMesh` is called with a valid `.glb`/`.gltf` path
- **THEN** the library SHALL add the triangulated geometry to the scene's mesh collection
  and report the number of triangles loaded

#### Scenario: Load an OBJ model
- **WHEN** `loadMesh` is called with a valid `.obj` path
- **THEN** the library SHALL add the triangulated geometry to the scene's mesh collection

#### Scenario: Non-triangular faces are triangulated
- **WHEN** an imported mesh contains quads or n-gons
- **THEN** the library SHALL triangulate them so the scene contains only triangles

#### Scenario: Missing or unreadable file
- **WHEN** `loadMesh` is called with a path that does not exist or cannot be parsed
- **THEN** the library SHALL report a descriptive error and leave the scene unchanged

### Requirement: Material assignment on import
The library SHALL allow a material to be assigned to imported geometry, either by naming a
material at load time or by mapping imported mesh/material names to scene materials.

#### Scenario: Assign a material by name at load
- **WHEN** `loadMesh` is called with a `material` argument naming a known material
- **THEN** the loaded geometry SHALL be assigned that material

#### Scenario: Unknown material name
- **WHEN** a material name is supplied that is not present in the scene
- **THEN** the library SHALL report an error rather than silently assigning a default

### Requirement: Up-axis normalization on import
The library SHALL normalize imported geometry into the core's right-handed Z-up frame
(Z = height), rotating Y-up sources such as glTF (and Y-up OBJ exports) accordingly, so all
scene geometry shares one convention regardless of source format.

#### Scenario: glTF Y-up mesh is rotated to Z-up
- **WHEN** a glTF mesh authored in Y-up is imported
- **THEN** the resulting scene geometry SHALL be expressed in the core Z-up frame, so a
  vertex at glTF `(x, y, z)` maps to core `(x, z, -y)` (or the documented equivalent
  right-handed rotation)

#### Scenario: Height is preserved as the Z component
- **WHEN** a building of known height is imported and its top vertices are inspected
- **THEN** the height SHALL appear as the Z coordinate in the scene, consistent with
  transmitter/receiver positions

### Requirement: Material definition import
The library SHALL load material definitions from a JSON file into the scene's material
table.

#### Scenario: Load materials from JSON
- **WHEN** `loadMaterials` is called with a JSON file describing materials and their
  electromagnetic parameters
- **THEN** each material SHALL be added to the scene and addressable by name

#### Scenario: Invalid materials JSON
- **WHEN** the materials JSON is malformed or missing required fields
- **THEN** the library SHALL report a descriptive error identifying the offending entry and
  SHALL NOT partially corrupt the material table

