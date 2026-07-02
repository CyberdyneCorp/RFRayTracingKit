# tiles3d-export Specification

## Purpose
TBD - created by archiving change core-io-formats. Update Purpose after archive.
## Requirements
### Requirement: 3D Tiles tileset export
The library SHALL export a minimal but valid 3D Tiles 1.1 `tileset.json` that references a single
glTF/glb produced by the existing glTF exporter, forming a single-tile tileset with no
hierarchical LOD.

#### Scenario: Tileset references a single glTF content
- **WHEN** a scene/result is exported to 3D Tiles
- **THEN** a `tileset.json` SHALL be written whose root tile has a `content.uri` pointing at a
  glTF/glb file that is also written

#### Scenario: Tileset is valid 3D Tiles 1.1
- **WHEN** the exported `tileset.json` is re-parsed
- **THEN** it SHALL have `asset.version` equal to `"1.1"`, a numeric root `geometricError`, and a
  root tile carrying a bounding volume and content

### Requirement: 3D Tiles single-tile documentation
The 3D Tiles export SHALL be documented as a single-tile tileset (no hierarchical level-of-detail).

#### Scenario: No hierarchical LOD is produced
- **WHEN** any scene is exported to 3D Tiles
- **THEN** the tileset SHALL contain exactly one content-bearing root tile and no child LOD tiles

### Requirement: Single-tile 3D Tiles export retained alongside LOD
The existing single-tile 3D Tiles export function (`exportPaths3DTiles`) SHALL be retained
unchanged, and the library SHALL additionally offer a separate hierarchical-LOD export variant
(`exportPaths3DTilesLod`, specified by the `tiles3d-lod` capability) so callers may choose a
single-tile tileset or a multi-tile LOD hierarchy.

#### Scenario: Single-tile export is unchanged
- **WHEN** `exportPaths3DTiles` is called on a scene/result
- **THEN** it SHALL still produce exactly one content-bearing root tile with no child LOD tiles,
  as before

#### Scenario: A hierarchical-LOD variant is available
- **WHEN** a caller needs view-dependent level-of-detail
- **THEN** the library SHALL provide a distinct hierarchical-LOD export entry point in addition to
  the single-tile function

