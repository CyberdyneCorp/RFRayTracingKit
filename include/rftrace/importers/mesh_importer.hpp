#pragma once

#include <cstddef>
#include <string>

#include "rftrace/scene.hpp"

namespace rftrace::io {

/// Import a triangle mesh (glTF/OBJ, via Assimp) into `scene`, normalizing the
/// geometry into the core Z-up frame. When `material` is non-empty it must name
/// an existing scene material. Returns the number of triangles added. Throws
/// SceneError on a missing/unparseable file or unknown material.
std::size_t importMesh(Scene& scene, const std::string& path,
                       const std::string& material = "");

}  // namespace rftrace::io
