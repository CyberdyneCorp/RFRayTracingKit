#pragma once

#include <string>

#include "rftrace/result.hpp"

namespace rftrace::io {

/// Export ray paths as a minimal but valid Cesium 3D Tiles 1.1 tileset.
///
/// Writes two files into `outputDir` (created if missing):
///   - `content.glb`  — a binary glTF of the ray-path geometry, produced by the
///                       existing glTF path exporter (reused verbatim).
///   - `tileset.json` — a single-tile 3D Tiles 1.1 tileset whose root tile has a
///                       bounding volume, a numeric geometricError, and a
///                       `content.uri` referencing `content.glb`.
///
/// This is a single-tile tileset: there is exactly one content-bearing root tile
/// and no hierarchical level-of-detail (no child LOD tiles). It is enough to load
/// in CesiumJS.
void exportPaths3DTiles(const RFResult& result, const std::string& outputDir,
                        bool includeReceivers = true);

/// Export ray paths as a hierarchical level-of-detail Cesium 3D Tiles 1.1
/// tileset (D5): a quadtree over the scene's horizontal (X/Y) extent.
///
/// The root tile covers the whole scene; it is recursively subdivided into up to
/// four child tiles per level (a quadtree of the horizontal extent), down to
/// `maxDepth`. Each non-empty tile owns the receivers (and their full ray paths)
/// whose horizontal position falls inside the tile's cell, and gets:
///   - its own `content.uri` referencing a `.glb` of just that geometry (written
///     with the reused glTF writer, then wrapped in a GLB container);
///   - a `box` `boundingVolume` (the tile's horizontal cell over the global
///     vertical extent), nested inside its parent's box;
///   - a numeric `geometricError` that strictly decreases with depth
///     (`baseError / 2^depth`), so viewers refine to finer tiles when close.
///
/// Refinement is `"REPLACE"`: the four children of a tile together cover the same
/// geometry as the parent at finer spatial granularity, so a viewer replaces the
/// parent content with its children when refining. Empty quadrants are omitted.
/// The written `tileset.json` is a valid 3D Tiles 1.1 hierarchy; loading it yields
/// a root with `children` (when `maxDepth >= 1` and geometry spans >1 cell), every
/// `content.uri` pointing at a `.glb` that exists on disk.
///
/// `maxDepth` is clamped to at least 1. The single-tile `exportPaths3DTiles`
/// remains available and unchanged.
void exportPaths3DTilesLod(const RFResult& result, const std::string& outputDir,
                           int maxDepth = 2, bool includeReceivers = true);

}  // namespace rftrace::io
