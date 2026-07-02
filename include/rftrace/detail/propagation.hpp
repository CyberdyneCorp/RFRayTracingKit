#pragma once

// Internal propagation helpers shared by the image-method and ray-launch
// engines. Not part of the public API.
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "rftrace/backend.hpp"
#include "rftrace/result.hpp"
#include "rftrace/rf/free_space_path_loss.hpp"
#include "rftrace/rf/phase.hpp"
#include "rftrace/scene.hpp"
#include "rftrace/simulator.hpp"

namespace rftrace::detail {

inline constexpr double kEps = 1e-4;

inline Vec3 mirrorPoint(const Vec3& p, const Vec3& planePoint,
                        const Vec3& unitNormal) {
  const double d = (p - planePoint).dot(unitNormal);
  return p - 2.0 * d * unitNormal;
}

/// Intersection of segment A→B with a plane; false if (near-)parallel or the hit
/// lies outside the open segment.
inline bool segmentPlane(const Vec3& a, const Vec3& b, const Vec3& planePoint,
                         const Vec3& n, Vec3& out) {
  const Vec3 ab = b - a;
  const double denom = n.dot(ab);
  if (std::abs(denom) < 1e-12) return false;
  const double t = n.dot(planePoint - a) / denom;
  if (t <= 1e-6 || t >= 1.0 - 1e-6) return false;
  out = a + t * ab;
  return true;
}

inline bool pointInTriangle(const Vec3& p, const Triangle& tri,
                            double eps = 1e-6) {
  const Vec3 v0 = tri.v2 - tri.v0;
  const Vec3 v1 = tri.v1 - tri.v0;
  const Vec3 v2 = p - tri.v0;
  const double d00 = v0.dot(v0), d01 = v0.dot(v1), d02 = v0.dot(v2);
  const double d11 = v1.dot(v1), d12 = v1.dot(v2);
  const double denom = d00 * d11 - d01 * d01;
  if (std::abs(denom) < 1e-20) return false;
  const double u = (d11 * d02 - d01 * d12) / denom;
  const double v = (d00 * d12 - d01 * d02) / denom;
  return u >= -eps && v >= -eps && (u + v) <= 1.0 + eps;
}

inline double pathLength(const std::vector<Vec3>& pts) {
  double len = 0.0;
  for (std::size_t i = 1; i < pts.size(); ++i)
    len += (pts[i] - pts[i - 1]).norm();
  return len;
}

/// Squared distance from point p to segment [a,b].
inline double distancePointToSegmentSq(const Vec3& p, const Vec3& a,
                                       const Vec3& b) {
  const Vec3 ab = b - a;
  const double len2 = ab.squaredNorm();
  if (len2 <= 0.0) return (p - a).squaredNorm();
  double t = (p - a).dot(ab) / len2;
  t = std::clamp(t, 0.0, 1.0);
  return (p - (a + t * ab)).squaredNorm();
}

/// Fill the RF metrics of a path whose geometry (`points`) and reflection loss
/// are already known.
inline void finishPath(RFPath& path, const Transmitter& tx, const Receiver& rx,
                       double reflectionLossDb) {
  const auto& pts = path.points;
  const double len = pathLength(pts);
  const double fspl = rf::freeSpacePathLossDb(len, tx.frequencyHz);

  const Vec3 departDir = pts[1] - pts.front();
  const Vec3 arriveDir = pts[pts.size() - 2] - pts.back();
  const double gtx = tx.antenna.gainTowards(departDir);
  const double grx = rx.antenna.gainTowards(arriveDir);

  path.pathLossDb = fspl + reflectionLossDb;
  path.receivedPowerDbm = tx.powerDbm + gtx + grx - path.pathLossDb;
  path.phaseRad = rf::propagationPhaseRad(len, tx.frequencyHz);
  path.delaySeconds = rf::propagationDelaySeconds(len);
}

// --- Engines (implemented across translation units) -------------------------

/// LOS path for a (tx, rx) pair if unobstructed.
std::optional<RFPath> losPath(const IBackend& backend, const Transmitter& tx,
                              const Receiver& rx);

/// Specular reflection paths (image method) up to `maxReflections` bounces.
std::vector<RFPath> imageMethodReflections(const Scene& scene,
                                           const IBackend& backend,
                                           const Transmitter& tx,
                                           const Receiver& rx,
                                           int maxReflections);

/// Ray-launched reflected paths per receiver (index-aligned to `receivers`).
/// LOS is handled separately by the caller.
std::vector<std::vector<RFPath>> rayLaunch(const Scene& scene,
                                           const IBackend& backend,
                                           const Transmitter& tx,
                                           const std::vector<Receiver>& receivers,
                                           const SimulationSettings& settings);

/// Aggregate a receiver's collected paths into its summary metrics.
void aggregate(ReceiverResult& rr, bool coherent);

}  // namespace rftrace::detail
