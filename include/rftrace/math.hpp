#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>

namespace rftrace {

/// Double-precision 3D vector, backed by Eigen. Used everywhere in the core so
/// RF code and backends share one type without a backend dependency.
using Vec3 = Eigen::Vector3d;

namespace constants {
/// Speed of light in vacuum (m/s).
inline constexpr double c = 299792458.0;
inline constexpr double pi = 3.14159265358979323846;
inline constexpr double two_pi = 2.0 * pi;
}  // namespace constants

/// A ray with a parametric validity interval [tMin, tMax]. Only intersections
/// whose distance t falls inside the interval are reported.
struct Ray {
  Vec3 origin{0.0, 0.0, 0.0};
  Vec3 direction{0.0, 0.0, 1.0};
  double tMin = 1e-4;
  double tMax = std::numeric_limits<double>::infinity();

  Ray() = default;
  Ray(const Vec3& o, const Vec3& d, double t_min = 1e-4,
      double t_max = std::numeric_limits<double>::infinity())
      : origin(o), direction(d), tMin(t_min), tMax(t_max) {}

  Vec3 at(double t) const { return origin + t * direction; }
};

/// Build a finite ray (segment) between two points, insetting both ends by an
/// epsilon so surfaces touching the endpoints do not self-occlude.
inline Ray segmentRay(const Vec3& from, const Vec3& to, double eps = 1e-4) {
  const Vec3 delta = to - from;
  const double len = delta.norm();
  const Vec3 dir = len > 0.0 ? (delta / len) : Vec3(0.0, 0.0, 1.0);
  return Ray(from, dir, eps, len - eps);
}

}  // namespace rftrace
