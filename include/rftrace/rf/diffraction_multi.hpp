#pragma once

// Multi-edge diffraction (ITU-R P.526): Bullington and Deygout constructions
// over a vertical-plane terrain+building profile. Both reuse the single
// knife-edge primitives (fresnelDiffractionParameter / knifeEdgeLossDb) from
// diffraction.hpp, so a profile with a single obstacle reduces EXACTLY to
// knifeEdgeLossDb(v) for that edge.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "rftrace/rf/diffraction.hpp"
#include "rftrace/rf/utd.hpp"

namespace rftrace::rf {

/// One sample of a profile in the vertical plane between tx and rx: the
/// horizontal distance from tx (m) and the obstacle-top height (m) at that
/// distance, in the same vertical datum as the endpoint heights.
struct ProfilePoint {
  double distanceMeters = 0.0;
  double heightMeters = 0.0;
};

/// A vertical-plane terrain+building profile for multi-edge diffraction.
/// `obstacles` are the ordered intermediate obstacle tops with
/// 0 < distance < totalDistanceMeters; the endpoints (tx at distance 0, rx at
/// totalDistanceMeters) carry their own heights. Metres in a common datum.
struct TerrainProfile {
  double totalDistanceMeters = 0.0;
  double txHeightMeters = 0.0;
  double rxHeightMeters = 0.0;
  std::vector<ProfilePoint> obstacles;
};

namespace detail {

/// Sentinel returned for a degenerate obstacle (outside its endpoints); large
/// and negative so it is never selected as a dominant edge and contributes no
/// loss via knifeEdgeLossDb.
inline constexpr double kNoEdgeV = -1e300;

/// Fresnel-Kirchhoff parameter v of obstacle `p` relative to the straight line
/// joining endpoints `txEnd` and `rxEnd` (each a distance/height pair). Positive
/// when the obstacle rises above that line.
inline double profileFresnelParameter(const ProfilePoint& txEnd,
                                       const ProfilePoint& rxEnd,
                                       const ProfilePoint& p,
                                       double wavelengthMeters) {
  const double span = rxEnd.distanceMeters - txEnd.distanceMeters;
  const double d1 = p.distanceMeters - txEnd.distanceMeters;
  const double d2 = rxEnd.distanceMeters - p.distanceMeters;
  if (span <= 0.0 || d1 <= 0.0 || d2 <= 0.0) return kNoEdgeV;
  const double lineHeight =
      txEnd.heightMeters +
      (rxEnd.heightMeters - txEnd.heightMeters) * (d1 / span);
  const double clearance = p.heightMeters - lineHeight;
  return fresnelDiffractionParameter(clearance, d1, d2, wavelengthMeters);
}

/// Deygout recursion: over obstacles [lo, hi) between endpoints `txEnd`/`rxEnd`,
/// take the dominant (max-v) edge, add its knife-edge loss, and recurse once on
/// each side (bounded by `depth`). Returns 0 when no edge in the range obstructs
/// the sub-line (knife-edge loss ≤ 0) or the range is empty / depth exhausted.
inline double deygoutRecurse(const std::vector<ProfilePoint>& obs,
                             std::size_t lo, std::size_t hi,
                             const ProfilePoint& txEnd,
                             const ProfilePoint& rxEnd, double wavelengthMeters,
                             int depth) {
  if (lo >= hi || depth <= 0) return 0.0;
  std::size_t best = lo;
  double bestV = kNoEdgeV;
  for (std::size_t i = lo; i < hi; ++i) {
    const double v = profileFresnelParameter(txEnd, rxEnd, obs[i], wavelengthMeters);
    if (v > bestV) {
      bestV = v;
      best = i;
    }
  }
  const double mainLoss = knifeEdgeLossDb(bestV);
  if (mainLoss <= 0.0) return 0.0;
  return mainLoss +
         deygoutRecurse(obs, lo, best, txEnd, obs[best], wavelengthMeters,
                        depth - 1) +
         deygoutRecurse(obs, best + 1, hi, obs[best], rxEnd, wavelengthMeters,
                        depth - 1);
}

/// UTD analogue of `deygoutRecurse`: identical dominant-edge selection, bounded
/// recursion and reciprocity, but the per-edge loss is the validated half-plane
/// UTD loss `utdDiffractionLossDb(v)` (a profile ridge is a half-plane) instead
/// of `knifeEdgeLossDb(v)`. Same termination (loss ≤ 0 ⇒ the sub-line is clear).
inline double utdDeygoutRecurse(const std::vector<ProfilePoint>& obs,
                                std::size_t lo, std::size_t hi,
                                const ProfilePoint& txEnd,
                                const ProfilePoint& rxEnd,
                                double wavelengthMeters, int depth) {
  if (lo >= hi || depth <= 0) return 0.0;
  std::size_t best = lo;
  double bestV = kNoEdgeV;
  for (std::size_t i = lo; i < hi; ++i) {
    const double v = profileFresnelParameter(txEnd, rxEnd, obs[i], wavelengthMeters);
    if (v > bestV) {
      bestV = v;
      best = i;
    }
  }
  const double mainLoss = utdDiffractionLossDb(bestV);
  if (mainLoss <= 0.0) return 0.0;
  return mainLoss +
         utdDeygoutRecurse(obs, lo, best, txEnd, obs[best], wavelengthMeters,
                           depth - 1) +
         utdDeygoutRecurse(obs, best + 1, hi, obs[best], rxEnd, wavelengthMeters,
                           depth - 1);
}

}  // namespace detail

/// Bullington equivalent-knife-edge diffraction loss (dB) for a profile.
///
/// Builds the tx-side and rx-side horizon rays from the steepest slopes to the
/// intermediate obstacles. When the tx horizon slope stays below the direct
/// tx→rx slope the path is line-of-sight and the loss is that of the single
/// most-obstructing Fresnel point; otherwise the two horizon rays intersect at
/// the Bullington point, whose equivalent knife-edge loss is returned.
///
/// A single-obstacle profile that pokes above the direct line reduces EXACTLY to
/// knifeEdgeLossDb(v) for that edge (the Bullington point coincides with it).
/// An empty profile returns 0.
inline double bullingtonLossDb(const TerrainProfile& profile,
                               double wavelengthMeters) {
  const double D = profile.totalDistanceMeters;
  if (profile.obstacles.empty() || D <= 0.0 || wavelengthMeters <= 0.0)
    return 0.0;
  const double txH = profile.txHeightMeters;
  const double rxH = profile.rxHeightMeters;
  const ProfilePoint txEnd{0.0, txH};
  const ProfilePoint rxEnd{D, rxH};

  double slopeTx = -1e300;
  double slopeRx = -1e300;
  double vMax = detail::kNoEdgeV;
  bool any = false;
  for (const ProfilePoint& p : profile.obstacles) {
    const double d = p.distanceMeters;
    if (d <= 0.0 || d >= D) continue;
    any = true;
    slopeTx = std::max(slopeTx, (p.heightMeters - txH) / d);
    slopeRx = std::max(slopeRx, (p.heightMeters - rxH) / (D - d));
    vMax = std::max(vMax,
                    detail::profileFresnelParameter(txEnd, rxEnd, p,
                                                    wavelengthMeters));
  }
  if (!any) return 0.0;

  const double slopeDirect = (rxH - txH) / D;
  if (slopeTx < slopeDirect) return knifeEdgeLossDb(vMax);  // line of sight

  // Trans-horizon: equivalent knife edge at the Bullington point.
  const double db = (rxH - txH + slopeRx * D) / (slopeTx + slopeRx);
  const double hb = txH + slopeTx * db;
  const double lineHeight = txH + (rxH - txH) * (db / D);
  const double v =
      fresnelDiffractionParameter(hb - lineHeight, db, D - db, wavelengthMeters);
  return knifeEdgeLossDb(v);
}

/// Deygout dominant-edge diffraction loss (dB) for a profile: the loss of the
/// dominant (max-v) edge plus one recursion on the tx-side and rx-side
/// sub-profiles. `maxRecursionDepth` bounds the recursion; the default (2)
/// yields the dominant edge plus one tx-side and one rx-side sub-edge.
///
/// A single-obstacle profile reduces EXACTLY to knifeEdgeLossDb(v) for that
/// edge (the sub-profiles are empty). An empty profile returns 0.
inline double deygoutLossDb(const TerrainProfile& profile,
                            double wavelengthMeters, int maxRecursionDepth = 2) {
  if (profile.obstacles.empty() || profile.totalDistanceMeters <= 0.0 ||
      wavelengthMeters <= 0.0)
    return 0.0;
  const ProfilePoint txEnd{0.0, profile.txHeightMeters};
  const ProfilePoint rxEnd{profile.totalDistanceMeters, profile.rxHeightMeters};
  return detail::deygoutRecurse(profile.obstacles, 0, profile.obstacles.size(),
                                txEnd, rxEnd, wavelengthMeters,
                                maxRecursionDepth);
}

/// UTD multi-edge (Deygout) diffraction loss (dB) for a profile: the exact
/// structure of `deygoutLossDb` (dominant max-v edge plus one tx-side and one
/// rx-side sub-recursion, bounded by `maxRecursionDepth`), but with the per-edge
/// loss = the validated half-plane UTD `utdDiffractionLossDb(v)` instead of the
/// ITU-R knife edge. Used for the doubly-obstructed UTD link the single-wedge
/// selection rejects. A single-obstacle profile reduces EXACTLY to
/// `utdDiffractionLossDb(v)` for that edge (empty sub-profiles); an empty profile
/// returns 0. Reciprocal and deterministic (inherited from the Deygout mirror).
inline double utdDeygoutLossDb(const TerrainProfile& profile,
                               double wavelengthMeters,
                               int maxRecursionDepth = 2) {
  if (profile.obstacles.empty() || profile.totalDistanceMeters <= 0.0 ||
      wavelengthMeters <= 0.0)
    return 0.0;
  const ProfilePoint txEnd{0.0, profile.txHeightMeters};
  const ProfilePoint rxEnd{profile.totalDistanceMeters, profile.rxHeightMeters};
  return detail::utdDeygoutRecurse(profile.obstacles, 0,
                                   profile.obstacles.size(), txEnd, rxEnd,
                                   wavelengthMeters, maxRecursionDepth);
}

}  // namespace rftrace::rf
