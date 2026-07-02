#pragma once

#include <cstddef>
#include <string>

#include "rftrace/scene.hpp"

namespace rftrace::io {

/// Import a CityJSON document into `scene`. `Building` and `BuildingPart` city
/// objects are triangulated from their solid/surface boundaries; a building
/// that has only a flat footprint surface plus a height attribute is extruded
/// via the shared footprint helper instead.
///
/// The document `transform` (scale/translate) is applied to the integer
/// vertices, which are then projected through the scene georeference (x is
/// treated as longitude, y as latitude, z as altitude in metres). When the
/// scene has no geographic origin it is set to the dataset centroid first.
///
/// `buildingMaterial` names the material assigned to the generated geometry; it
/// is created from a preset (or a neutral default) if the scene lacks it.
/// Returns the number of triangles added. Throws SceneError when the file is
/// missing or is not a valid CityJSON document (the scene is left unchanged).
std::size_t importCityJSON(Scene& scene, const std::string& path,
                           const std::string& buildingMaterial = "concrete");

}  // namespace rftrace::io
