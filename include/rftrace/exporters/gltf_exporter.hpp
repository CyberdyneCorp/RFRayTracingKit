#pragma once

#include <string>

#include "rftrace/result.hpp"

namespace rftrace::io {

/// Export ray paths as glTF 2.0 line geometry (one polyline per path, vertices
/// colored by received power). When `includeReceivers` is true, receiver
/// positions are added as a point primitive. Writes a self-contained `.gltf`
/// with an embedded base64 buffer.
void exportPathsGltf(const RFResult& result, const std::string& path,
                     bool includeReceivers = true);

/// Build the glTF document as a JSON string (used by the file writer and tests).
std::string pathsToGltfString(const RFResult& result,
                              bool includeReceivers = true);

}  // namespace rftrace::io
