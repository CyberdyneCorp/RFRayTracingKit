# tiles3d-lod Specification

## Purpose
TBD - created by archiving change io-python-and-formats. Update Purpose after archive.
## Requirements
### Requirement: Hierarchical-LOD 3D Tiles tileset export
The library SHALL provide `exportPaths3DTilesLod(result, directory, maxDepth)` that writes a valid
3D Tiles 1.1 `tileset.json` describing a TILE TREE — a root tile recursively subdivided into child
tiles by a quadtree over the scene's horizontal extent down to `maxDepth` — where each
content-bearing tile references its own glb holding the geometry/paths within that tile's bounds.
The existing single-tile `exportPaths3DTiles` function SHALL be retained.

#### Scenario: A tile tree with child tiles is produced
- **WHEN** a result is exported with `exportPaths3DTilesLod(result, directory, maxDepth)` and
  `maxDepth` greater than zero
- **THEN** the written `tileset.json` root tile SHALL have one or more `children`, forming a
  quadtree hierarchy rather than a single tile

#### Scenario: Each content tile references its own glb
- **WHEN** the exported tile tree is parsed
- **THEN** every tile carrying a `content.uri` SHALL reference a `.glb` file that was written into
  the directory

### Requirement: 3D Tiles LOD bounding volumes and geometric error
Each tile in the exported hierarchy SHALL carry a `boundingVolume` (box or region) enclosing its
content, and its `geometricError` SHALL decrease with depth so that deeper (finer) tiles have a
smaller geometric error than their ancestors.

#### Scenario: Child geometric error is smaller than the parent's
- **WHEN** a parent tile and its child tiles are read from the exported tileset
- **THEN** each child's `geometricError` SHALL be strictly less than its parent's

#### Scenario: Every tile carries a bounding volume
- **WHEN** any tile in the exported hierarchy is read
- **THEN** it SHALL carry a `boundingVolume` (box or region) enclosing that tile's content

### Requirement: 3D Tiles LOD validity and documented refine mode
The exported hierarchy SHALL be valid 3D Tiles 1.1 with `asset.version` equal to `"1.1"` and a
numeric root `geometricError`, and the tile `refine` mode ("ADD" or "REPLACE") used by the
exporter SHALL be documented, so a viewer loading `tileset.json` obtains a valid, refinable
hierarchy.

#### Scenario: Exported LOD tileset is valid 3D Tiles 1.1
- **WHEN** the exported `tileset.json` is re-parsed
- **THEN** it SHALL have `asset.version` equal to `"1.1"`, a numeric root `geometricError`, and a
  root tile with a bounding volume

#### Scenario: Refine mode is documented and set
- **WHEN** a tile in the exported hierarchy is read
- **THEN** it SHALL carry a `refine` value of "ADD" or "REPLACE" consistent with the exporter's
  documented refinement mode

