#pragma once

#include <string>

#include "rftrace/result.hpp"

namespace rftrace {
class Scene;

namespace io {

/// Build a Cesium CZML document (JSON array of packets) from an RF result:
/// a document packet, one point packet per receiver (received power in the
/// description), and one polyline packet per ray path.
///
/// Positions are encoded as `cartesian` local ENU metres. Use the Scene overload
/// to emit WGS84 `cartographicDegrees` when the scene is georeferenced.
std::string resultToCzmlString(const RFResult& result);

/// Scene-aware overload: when `scene` is georeferenced, positions are encoded as
/// `cartographicDegrees` (longitude, latitude, height) via the inverse of the
/// scene georeference (D1); otherwise `cartesian` local coordinates are used.
std::string resultToCzmlString(const RFResult& result, const Scene& scene);

/// Write the CZML document to `path` (cartesian local coordinates).
void exportResultCzml(const RFResult& result, const std::string& path);

/// Write the CZML document to `path`, honouring the scene georeference.
void exportResultCzml(const RFResult& result, const std::string& path,
                      const Scene& scene);

}  // namespace io
}  // namespace rftrace
