#include "rftrace/geo/footprint.hpp"

#include <algorithm>

namespace rftrace::geo {

namespace {
/// Drop a repeated closing vertex so the ring holds only distinct corners.
std::vector<Vec3> openRing(const std::vector<Vec3>& ring) {
  if (ring.size() >= 2 &&
      ring.front().x() == ring.back().x() &&
      ring.front().y() == ring.back().y()) {
    return std::vector<Vec3>(ring.begin(), ring.end() - 1);
  }
  return ring;
}
}  // namespace

void extrudeFootprint(const std::vector<Vec3>& ring, double baseZ,
                      double height, std::vector<Triangle>& out) {
  std::vector<Vec3> r = openRing(ring);
  const std::size_t n = r.size();
  if (n < 3) return;

  // Canonicalize to CCW so wall normals face outward and roof normals face +Z,
  // regardless of the source ring orientation (GeoJSON/OSM winding varies).
  double area2 = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const Vec3& a = r[i];
    const Vec3& b = r[(i + 1) % n];
    area2 += a.x() * b.y() - b.x() * a.y();
  }
  if (area2 < 0.0) std::reverse(r.begin(), r.end());

  const double zTop = baseZ + height;
  out.reserve(out.size() + 2 * n + (n - 2));

  // Walls: two triangles per edge, wound outward for a CCW ring.
  for (std::size_t i = 0; i < n; ++i) {
    const Vec3& a = r[i];
    const Vec3& b = r[(i + 1) % n];
    const Vec3 a0(a.x(), a.y(), baseZ);
    const Vec3 b0(b.x(), b.y(), baseZ);
    const Vec3 a1(a.x(), a.y(), zTop);
    const Vec3 b1(b.x(), b.y(), zTop);
    out.push_back(Triangle{a0, b0, b1});
    out.push_back(Triangle{a0, b1, a1});
  }

  // Flat roof: fan triangulation of the ring at the top elevation.
  for (std::size_t i = 1; i + 1 < n; ++i) {
    out.push_back(Triangle{Vec3(r[0].x(), r[0].y(), zTop),
                           Vec3(r[i].x(), r[i].y(), zTop),
                           Vec3(r[i + 1].x(), r[i + 1].y(), zTop)});
  }
}

std::vector<Triangle> extrudeFootprint(const std::vector<Vec3>& ring,
                                       double baseZ, double height) {
  std::vector<Triangle> out;
  extrudeFootprint(ring, baseZ, height, out);
  return out;
}

}  // namespace rftrace::geo
