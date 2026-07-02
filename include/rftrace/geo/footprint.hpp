#pragma once

#include <vector>

#include "rftrace/geometry.hpp"
#include "rftrace/math.hpp"

namespace rftrace::geo {

/// Extrude a closed polygon footprint into a solid: vertical walls plus a flat
/// roof. `ring` is the footprint's outer boundary in local ENU metres; only the
/// x/y of each vertex is used (its z is ignored). A repeated closing vertex
/// (ring.front() == ring.back()) is tolerated and dropped. The walls run from
/// `baseZ` up to `baseZ + height`; the roof is a fan triangulation of the ring
/// at `baseZ + height`. Winding is such that wall/roof normals face outward for
/// a counter-clockwise ring (the OSM/GeoJSON convention). Returns the generated
/// triangles; a ring with fewer than 3 distinct vertices yields none.
std::vector<Triangle> extrudeFootprint(const std::vector<Vec3>& ring,
                                       double baseZ, double height);

/// In-place variant: appends the extruded triangles to `out`.
void extrudeFootprint(const std::vector<Vec3>& ring, double baseZ,
                      double height, std::vector<Triangle>& out);

}  // namespace rftrace::geo
