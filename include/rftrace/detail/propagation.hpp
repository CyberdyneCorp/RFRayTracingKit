#pragma once

// Internal propagation helpers shared by the image-method and ray-launch
// engines. Not part of the public API.
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include <cctype>
#include <string>

#include "rftrace/backend.hpp"
#include "rftrace/result.hpp"
#include "rftrace/rf/atmospheric.hpp"
#include "rftrace/rf/free_space_path_loss.hpp"
#include "rftrace/rf/phase.hpp"
#include "rftrace/rf/polarization.hpp"
#include "rftrace/rf/vegetation.hpp"
#include "rftrace/scene.hpp"
#include "rftrace/simulator.hpp"

namespace rftrace::detail {

inline constexpr double kEps = 1e-4;

/// Upper bound on ray-march iterations when walking a segment through the scene
/// (vegetation depth / occlusion). Guards against pathological geometry.
inline constexpr int kMaxMarchSteps = 4096;

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

/// Shared, backend-agnostic context threaded into path building/finishing so
/// Phase 7 loss terms (diffraction / atmospheric / vegetation) can be applied
/// uniformly to every path. Holds only borrowed references; must not outlive
/// the objects it points at. Passed as an optional pointer so pre-Phase-7
/// callers (which pass nullptr) keep identical behavior.
struct PropagationContext {
  const SimulationSettings& settings;
  const Scene& scene;
  const IBackend& backend;
  double frequencyHz = 0.0;
};

/// True when a material is tagged as vegetation (name contains "vegetation",
/// case-insensitive). R5 tags foliage by material; explicit volumes deferred.
inline bool isVegetationMaterial(const Material& mat) {
  std::string lower = mat.name;
  for (char& ch : lower) ch = static_cast<char>(std::tolower(
                             static_cast<unsigned char>(ch)));
  return lower.find("vegetation") != std::string::npos;
}

/// Length (m) of the segment [a,b] that lies inside vegetation-tagged geometry.
/// Marches the segment collecting every crossing of a vegetation triangle,
/// sorts them, and pairs consecutive crossings (enter/exit) into interior
/// spans. Assumes vegetation meshes are closed volumes; an unpaired trailing
/// crossing (grazing / endpoint inside foliage) is ignored.
inline double segmentVegetationDepthMeters(const Scene& scene,
                                           const IBackend& backend,
                                           const Vec3& a, const Vec3& b) {
  const Vec3 delta = b - a;
  const double len = delta.norm();
  if (len <= kEps) return 0.0;
  const Vec3 dir = delta / len;

  std::vector<double> crossings;
  double cursor = 0.0;
  for (int step = 0; step < kMaxMarchSteps; ++step) {
    const Ray ray(a, dir, cursor + kEps, len - kEps);
    if (ray.tMax <= ray.tMin) break;
    const Hit h = backend.closestHit(ray);
    if (!h.valid || h.t >= len - kEps) break;
    if (h.triangle >= 0 &&
        isVegetationMaterial(scene.materialForTriangle(h.triangle)))
      crossings.push_back(h.t);
    cursor = h.t + kEps;
  }

  std::sort(crossings.begin(), crossings.end());
  double depth = 0.0;
  for (std::size_t i = 0; i + 1 < crossings.size(); i += 2)
    depth += crossings[i + 1] - crossings[i];
  return depth;
}

/// Total in-foliage depth (m) over a whole path polyline.
inline double vegetationDepthMeters(const PropagationContext& ctx,
                                    const std::vector<Vec3>& points) {
  double depth = 0.0;
  for (std::size_t i = 1; i < points.size(); ++i)
    depth += segmentVegetationDepthMeters(ctx.scene, ctx.backend, points[i - 1],
                                          points[i]);
  return depth;
}

/// True if any NON-vegetation triangle blocks `ray`. Vegetation is transparent
/// to this test: it attenuates (via the foliage term) rather than blocking, so
/// a link may pass through foliage. Used only when vegetation is enabled;
/// otherwise callers use the backend's plain occluded().
inline bool blockedByNonVegetation(const Scene& scene, const IBackend& backend,
                                   const Ray& ray) {
  double cursor = ray.tMin;
  for (int step = 0; step < kMaxMarchSteps; ++step) {
    const Ray probe(ray.origin, ray.direction, cursor, ray.tMax);
    if (probe.tMax <= probe.tMin) return false;
    const Hit h = backend.closestHit(probe);
    if (!h.valid) return false;
    if (h.triangle < 0 ||
        !isVegetationMaterial(scene.materialForTriangle(h.triangle)))
      return true;
    cursor = h.t + kEps;
  }
  return false;
}

/// Sum of the additive Phase 7 propagation loss terms (dB) for a path with the
/// given polyline geometry. This is the single hook where atmospheric (rain /
/// gaseous, ITU-R P.838 / P.676) and vegetation (Weissberger / P.833) losses
/// are added per path.
///
/// Every term is gated by a default-off flag, so with default settings this
/// returns 0 and the per-path budget is bit-for-bit identical to Phase 1/2.
inline double extraPropagationLossDb(const PropagationContext& ctx,
                                     const std::vector<Vec3>& points) {
  const SimulationSettings& s = ctx.settings;
  const double freqHz = ctx.frequencyHz;
  double extra = 0.0;

  if (s.enableRain || s.enableGaseousAttenuation) {
    const double lengthKm = pathLength(points) / 1000.0;
    if (s.enableRain)
      extra += rf::rainSpecificAttenuationDbPerKm(freqHz, s.rainRateMmPerHr) *
               lengthKm;
    if (s.enableGaseousAttenuation)
      extra += rf::gaseousSpecificAttenuationDbPerKm(freqHz) * lengthKm;
  }

  if (s.enableVegetation) {
    const double depth = vegetationDepthMeters(ctx, points);
    if (depth > 0.0) extra += rf::foliageLossDb(freqHz, depth);
  }

  return extra;
}

/// Fill the RF metrics of a path whose geometry (`points`) and reflection loss
/// are already known. When `ctx` is non-null, additive Phase 7 loss terms are
/// folded into the budget (zero in this foundation, so behavior is unchanged).
inline void finishPath(RFPath& path, const Transmitter& tx, const Receiver& rx,
                       double reflectionLossDb,
                       const PropagationContext* ctx = nullptr,
                       const rf::Jones* incidentJones = nullptr) {
  const auto& pts = path.points;
  const double len = pathLength(pts);
  const double fspl = rf::freeSpacePathLossDb(len, tx.frequencyHz);

  const Vec3 departDir = pts[1] - pts.front();
  const Vec3 arriveDir = pts[pts.size() - 2] - pts.back();
  // Use the steered array gain when an array is configured, else the single
  // antenna pattern. Array steering defaults to the path's own direction.
  const double gtx =
      tx.array && tx.array->size() > 0
          ? rf::steeredGainDbi(
                *tx.array,
                tx.beamSteering.norm() > 0.0 ? tx.beamSteering : departDir,
                departDir)
          : tx.antenna.gainTowards(departDir);
  const double grx =
      rx.array && rx.array->size() > 0
          ? rf::steeredGainDbi(
                *rx.array,
                rx.beamSteering.norm() > 0.0 ? rx.beamSteering : arriveDir,
                arriveDir)
          : rx.antenna.gainTowards(arriveDir);

  const double extraDb = ctx ? extraPropagationLossDb(*ctx, pts) : 0.0;

  // Polarization mismatch (D3). The path's polarization defaults to the
  // transmitter's (co-polar); the mismatch against the receiver antenna is
  // added to the budget. With the default Vertical/Vertical (or `None`) states
  // this is exactly 0 dB, leaving the archived budget bit-for-bit unchanged.
  // Reflected paths pass the depolarized arriving Jones state (accumulated
  // through the bounces); other paths default to the transmitter's polarization.
  path.polarization = incidentJones ? *incidentJones : rf::jonesFor(tx.polarization);
  const double polMismatchDb =
      (tx.polarization == Polarization::None ||
       rx.polarization == Polarization::None)
          ? 0.0
          : rf::polarizationMismatchDb(path.polarization,
                                       rf::jonesFor(rx.polarization));

  path.pathLossDb = fspl + reflectionLossDb + extraDb + polMismatchDb;
  path.receivedPowerDbm = tx.powerDbm + gtx + grx - path.pathLossDb;
  path.phaseRad = rf::propagationPhaseRad(len, tx.frequencyHz);
  path.delaySeconds = rf::propagationDelaySeconds(len);
}

/// Build the direct LOS RFPath for a (tx, rx) pair whose direct segment is
/// already known to be clear. This is the exact tail of `losPath` factored out
/// so the batched LOS sites and the per-ray `losPath` produce byte-identical
/// paths.
inline RFPath buildLosPath(const Transmitter& tx, const Receiver& rx,
                           const PropagationContext* ctx = nullptr) {
  RFPath p;
  p.transmitterId = tx.id;
  p.receiverId = rx.id;
  p.type = PathType::LOS;
  p.points = {tx.position, rx.position};
  finishPath(p, tx, rx, 0.0, ctx);
  return p;
}

// --- Engines (implemented across translation units) -------------------------

/// LOS path for a (tx, rx) pair if unobstructed. `ctx` (optional) carries the
/// Phase 7 loss hooks; nullptr reproduces pre-Phase-7 behavior.
std::optional<RFPath> losPath(const IBackend& backend, const Transmitter& tx,
                              const Receiver& rx,
                              const PropagationContext* ctx = nullptr);

/// Specular reflection paths (image method) up to `maxReflections` bounces.
std::vector<RFPath> imageMethodReflections(const Scene& scene,
                                           const IBackend& backend,
                                           const Transmitter& tx,
                                           const Receiver& rx,
                                           int maxReflections,
                                           const PropagationContext* ctx = nullptr);

/// Ray-launched reflected paths per receiver (index-aligned to `receivers`).
/// LOS is handled separately by the caller.
std::vector<std::vector<RFPath>> rayLaunch(const Scene& scene,
                                           const IBackend& backend,
                                           const Transmitter& tx,
                                           const std::vector<Receiver>& receivers,
                                           const SimulationSettings& settings,
                                           const PropagationContext* ctx = nullptr);

/// Sequential per-ray reference implementation of `rayLaunch`, retained as the
/// bit-for-bit equality oracle for the batched wavefront rewrite. Internal
/// (detail-only) test symbol; not part of the public API.
std::vector<std::vector<RFPath>> rayLaunchReference(
    const Scene& scene, const IBackend& backend, const Transmitter& tx,
    const std::vector<Receiver>& receivers, const SimulationSettings& settings,
    const PropagationContext* ctx = nullptr);

/// Single dominant knife-edge diffracted path (ITU-R P.526) for a (tx, rx) pair
/// whose direct LOS is blocked. Searches the scene's open silhouette (boundary)
/// edges for the strongest single-edge detour and returns it as a
/// PathType::Diffraction path (diffraction count 1), or nullopt when none is
/// found. Only invoked when settings.enableDiffraction is set.
std::optional<RFPath> diffractionPath(const Scene& scene,
                                      const IBackend& backend,
                                      const Transmitter& tx, const Receiver& rx,
                                      double frequencyHz,
                                      const PropagationContext* ctx = nullptr);

/// Aggregate a receiver's collected paths into its summary metrics.
void aggregate(ReceiverResult& rr, bool coherent);

}  // namespace rftrace::detail
