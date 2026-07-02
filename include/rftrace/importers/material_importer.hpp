#pragma once

#include <cstddef>
#include <string>

#include "rftrace/scene.hpp"

namespace rftrace::io {

/// Load material definitions from a JSON file into the scene's material table.
/// Accepts either a top-level array or an object with a "materials" array.
/// Returns the number of materials loaded. Throws SceneError on invalid JSON.
std::size_t importMaterials(Scene& scene, const std::string& path);

}  // namespace rftrace::io
