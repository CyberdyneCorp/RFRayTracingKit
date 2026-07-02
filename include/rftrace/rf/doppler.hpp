#pragma once

// Doppler (D4): the per-path Doppler frequency shift induced by receiver motion.
//
// For a receiver moving with velocity `v` (m/s) and a path arriving from unit
// direction `khat` (pointing from the receiver toward the last hop, i.e. toward
// the source of that path), the classical (non-relativistic) Doppler shift is
//
//     f_d = (v . khat) / c * f
//
// The sign convention follows `khat` pointing toward the arrival: a receiver
// closing on the source (velocity aligned with `khat`) yields a POSITIVE shift;
// a receding receiver yields a negative shift; purely transverse motion yields
// zero. A static receiver (v = 0) yields exactly 0, keeping archived results
// unchanged.

#include "rftrace/math.hpp"

namespace rftrace::rf {

/// Per-path Doppler shift (Hz): (v . khat)/c * f.
///
/// `receiverVelocity` is the receiver velocity vector (m/s). `arrivalDirUnit` is
/// the arrival direction (from the receiver toward the last hop); it need not be
/// pre-normalized — it is normalized internally. Returns 0 for a non-positive
/// frequency or a degenerate (zero-length) arrival direction.
inline double perPathDopplerHz(const Vec3& receiverVelocity,
                               const Vec3& arrivalDirUnit, double frequencyHz) {
  if (frequencyHz <= 0.0) return 0.0;
  const double norm = arrivalDirUnit.norm();
  if (norm <= 0.0) return 0.0;
  const Vec3 khat = arrivalDirUnit / norm;
  return receiverVelocity.dot(khat) / constants::c * frequencyHz;
}

}  // namespace rftrace::rf
