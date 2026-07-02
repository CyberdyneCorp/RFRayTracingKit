#include "rftrace/geometry.hpp"

namespace rftrace {

Hit closestHitBruteForce(const std::vector<Triangle>& triangles,
                         const Ray& ray) {
  Hit best;
  for (std::size_t i = 0; i < triangles.size(); ++i) {
    Ray r = ray;
    r.tMax = best.valid ? best.t : ray.tMax;
    const Hit h = intersectTriangle(r, triangles[i], static_cast<int>(i));
    if (h.valid && h.t < best.t) best = h;
  }
  return best;
}

bool occludedBruteForce(const std::vector<Triangle>& triangles,
                        const Ray& ray) {
  for (std::size_t i = 0; i < triangles.size(); ++i) {
    if (intersectTriangle(ray, triangles[i], static_cast<int>(i)).valid)
      return true;
  }
  return false;
}

}  // namespace rftrace
