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

}  // namespace rftrace::io
