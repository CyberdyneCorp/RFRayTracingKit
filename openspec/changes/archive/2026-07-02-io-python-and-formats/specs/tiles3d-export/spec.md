## ADDED Requirements

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
