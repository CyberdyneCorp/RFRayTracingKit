#pragma once

#include <limits>
#include <vector>

#include "rftrace/math.hpp"

namespace rftrace {

/// A single triangle in the scene's Z-up frame.
struct Triangle {
  Vec3 v0{0, 0, 0};
  Vec3 v1{1, 0, 0};
  Vec3 v2{0, 1, 0};

  Vec3 edge1() const { return v1 - v0; }
  Vec3 edge2() const { return v2 - v0; }

  /// Geometric (unnormalized) normal; zero-length for a degenerate triangle.
  Vec3 rawNormal() const { return edge1().cross(edge2()); }

  /// Unit normal. Returns a zero vector for a degenerate triangle.
  Vec3 normal() const {
    const Vec3 n = rawNormal();
    const double len = n.norm();
    return len > 0.0 ? (n / len) : Vec3(0, 0, 0);
  }

  Vec3 centroid() const { return (v0 + v1 + v2) / 3.0; }
};

/// Result of a ray query. `valid` is false for a miss.
struct Hit {
  bool valid = false;
  double t = std::numeric_limits<double>::infinity();
  double u = 0.0;  ///< barycentric coordinate of v1
  double v = 0.0;  ///< barycentric coordinate of v2
  int triangle = -1;

  Vec3 point(const Ray& ray) const { return ray.at(t); }
};

/// Möller–Trumbore ray–triangle intersection. Reports the hit distance and
/// barycentric coordinates. Returns an invalid hit for parallel or degenerate
/// cases (never divides by zero, never produces NaN).
inline Hit intersectTriangle(const Ray& ray, const Triangle& tri,
                             int triIndex = -1) {
  constexpr double kEps = 1e-12;
  const Vec3 e1 = tri.edge1();
  const Vec3 e2 = tri.edge2();
  const Vec3 pvec = ray.direction.cross(e2);
  const double det = e1.dot(pvec);

  // Ray parallel to the triangle plane (or triangle degenerate).
  if (det > -kEps && det < kEps) return {};

  const double invDet = 1.0 / det;
  const Vec3 tvec = ray.origin - tri.v0;
  const double u = tvec.dot(pvec) * invDet;
  if (u < 0.0 || u > 1.0) return {};

  const Vec3 qvec = tvec.cross(e1);
  const double v = ray.direction.dot(qvec) * invDet;
  if (v < 0.0 || u + v > 1.0) return {};

  const double t = e2.dot(qvec) * invDet;
  if (t < ray.tMin || t > ray.tMax) return {};

  return Hit{true, t, u, v, triIndex};
}

/// Axis-aligned bounding box.
struct AABB {
  Vec3 lo{std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::infinity()};
  Vec3 hi{-std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity()};

  void expand(const Vec3& p) { lo = lo.cwiseMin(p); hi = hi.cwiseMax(p); }
  void expand(const AABB& b) { lo = lo.cwiseMin(b.lo); hi = hi.cwiseMax(b.hi); }
  void expand(const Triangle& t) { expand(t.v0); expand(t.v1); expand(t.v2); }

  Vec3 center() const { return 0.5 * (lo + hi); }
  Vec3 extent() const { return hi - lo; }
  bool empty() const { return (hi.array() < lo.array()).any(); }

  double surfaceArea() const {
    if (empty()) return 0.0;
    const Vec3 d = hi - lo;
    return 2.0 * (d.x() * d.y() + d.y() * d.z() + d.z() * d.x());
  }

  /// Slab test. Returns true if the ray enters the box within its interval and
  /// writes the near intersection distance to `tNear`.
  bool intersect(const Ray& ray, double& tNear) const {
    double t0 = ray.tMin;
    double t1 = ray.tMax;
    for (int a = 0; a < 3; ++a) {
      const double inv = 1.0 / ray.direction[a];
      double tA = (lo[a] - ray.origin[a]) * inv;
      double tB = (hi[a] - ray.origin[a]) * inv;
      if (tA > tB) std::swap(tA, tB);
      t0 = tA > t0 ? tA : t0;
      t1 = tB < t1 ? tB : t1;
      if (t0 > t1) return false;
    }
    tNear = t0;
    return true;
  }
};

/// Brute-force closest-hit over all triangles. Reference for validating the BVH.
Hit closestHitBruteForce(const std::vector<Triangle>& triangles, const Ray& ray);

/// Brute-force occlusion test over all triangles. Reference for the BVH.
bool occludedBruteForce(const std::vector<Triangle>& triangles, const Ray& ray);

}  // namespace rftrace
