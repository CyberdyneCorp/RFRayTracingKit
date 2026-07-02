#pragma once

#include <cstddef>
#include <string>

#include "rftrace/scene.hpp"

namespace rftrace::io {

/// Options controlling GeoJSON import.
struct GeoJsonImportOptions {
  /// Material assigned to extruded building meshes. A per-feature `material`
  /// property overrides this. The material is created (from a built-in preset,
  /// else a neutral default) if the scene does not already define it.
  std::string buildingMaterial = "concrete";
  /// How to treat `Point` features: "receiver", "transmitter", or "" to ignore
  /// points entirely. A per-feature `type`/`layer` property ("transmitter" or
  /// "receiver") overrides this default.
  std::string pointType = "receiver";
  /// Fallback building height (metres) when a feature has neither a `height`
  /// nor a `levels`/`building:levels` property.
  double defaultHeight = 6.0;
};

/// Import a GeoJSON `FeatureCollection` into `scene`. `Polygon` and
/// `MultiPolygon` features become buildings extruded (via the shared footprint
/// helper) to a height from the `height` property, else `levels`/
/// `building:levels` times a 3 m storey height, else `opts.defaultHeight`.
/// `Point` features become receivers or transmitters per `opts.pointType`.
///
/// Coordinates are projected through the scene georeference; when the scene has
/// no geographic origin it is set to the dataset centroid first. Returns the
/// number of building triangles added. Throws SceneError when the file is
/// missing, is not valid GeoJSON, or contains no usable geometry (the scene is
/// left unchanged in that case).
std::size_t importGeoJSON(Scene& scene, const std::string& path,
                          const GeoJsonImportOptions& opts = {});

}  // namespace rftrace::io
