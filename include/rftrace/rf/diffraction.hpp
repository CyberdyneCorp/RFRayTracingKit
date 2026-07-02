#pragma once

#include <cmath>

#include "rftrace/math.hpp"

namespace rftrace::rf {

/// ITU-R P.526 single knife-edge diffraction loss J(v) in dB, from the
/// Fresnel-Kirchhoff diffraction parameter v.
///
///   J(v) = 6.9 + 20·log10( sqrt((v-0.1)² + 1) + (v-0.1) )   for v > -0.78
///   J(v) = 0                                                for v ≤ -0.78
///
/// Reference points: J(0) ≈ 6.03 dB (ray grazing the edge); J increases
/// monotonically as v grows (receiver deeper into the geometric shadow); and it
/// falls to 0 for v ≲ -0.78 (ample Fresnel clearance / clear line of sight).
/// The result is clamped at 0 so a clear path never reports a spurious gain.
inline double knifeEdgeLossDb(double v) {
  if (v <= -0.78) return 0.0;
  const double t = v - 0.1;
  const double loss = 6.9 + 20.0 * std::log10(std::sqrt(t * t + 1.0) + t);
  return loss > 0.0 ? loss : 0.0;
}

/// Fresnel-Kirchhoff diffraction parameter
///   v = h · sqrt( 2/λ · (1/d1 + 1/d2) )
/// where `clearanceMeters` (h) is the height of the obstacle edge above the
/// direct tx→rx line (positive into the shadow) and d1/d2 are the tx→edge and
/// edge→rx distances (m). Returns 0 for degenerate (non-positive) distances or
/// wavelength.
inline double fresnelDiffractionParameter(double clearanceMeters, double d1,
                                          double d2, double wavelengthMeters) {
  if (d1 <= 0.0 || d2 <= 0.0 || wavelengthMeters <= 0.0) return 0.0;
  return clearanceMeters *
         std::sqrt(2.0 / wavelengthMeters * (1.0 / d1 + 1.0 / d2));
}

/// Geometry of a candidate knife edge relative to the direct tx→rx line.
struct DiffractionGeometry {
  double clearanceMeters = 0.0;  ///< perpendicular height of the edge above LOS
  double d1 = 0.0;               ///< tx→edge distance projected onto LOS
  double d2 = 0.0;               ///< edge→rx distance projected onto LOS
};

/// Resolve the clearance height and along-path distances of `edgePoint` relative
/// to the direct tx→rx line. `clearanceMeters` is the perpendicular offset of the
/// edge point from that line (always ≥ 0); d1/d2 are its projections onto the
/// line. Returns all-zero for a degenerate (co-located) tx/rx pair.
inline DiffractionGeometry diffractionGeometry(const Vec3& tx, const Vec3& rx,
                                               const Vec3& edgePoint) {
  const Vec3 los = rx - tx;
  const double distance = los.norm();
  if (distance <= 0.0) return {};
  const Vec3 unit = los / distance;
  const double proj = (edgePoint - tx).dot(unit);
  const Vec3 foot = tx + proj * unit;
  DiffractionGeometry g;
  g.clearanceMeters = (edgePoint - foot).norm();
  g.d1 = proj;
  g.d2 = distance - proj;
  return g;
}

}  // namespace rftrace::rf
