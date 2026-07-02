#pragma once

#include <algorithm>
#include <cmath>

namespace rftrace::rf {

// Vegetation (foliage) excess loss, Phase 7 / R5.
//
// Weissberger's Modified Exponential Decay (MED) model — a documented, widely
// used foliage-loss approximation (also referenced by ITU-R P.833 for through-
// foliage paths). Loss depends on the in-foliage depth d (m) and frequency
// f (GHz), and is capped at a physically reasonable maximum.
//
//   L(dB) = 0.45 · f^0.284 · d               for 0 ≤ d ≤ 14 m
//   L(dB) = 1.33 · f^0.284 · d^0.588         for 14 < d ≤ 400 m
//
// Valid roughly 0.23–95 GHz. The d^0.588 regime is sub-linear: doubling the
// foliage depth less than doubles the loss. Depth 0 → 0 dB.

namespace detail {
/// Default upper bound (dB) on foliage excess loss. Beyond deep foliage the
/// Weissberger fit is no longer physically meaningful, so the result is
/// clamped to keep the link budget sane.
inline constexpr double kFoliageMaxLossDb = 40.0;
}  // namespace detail

/// Weissberger MED foliage loss (dB) for an in-foliage depth `depthMeters` at
/// frequency `frequencyHz`, clamped to [0, `maxLossDb`]. Returns 0 for a
/// non-positive depth or frequency.
inline double foliageLossDb(double frequencyHz, double depthMeters,
                            double maxLossDb = detail::kFoliageMaxLossDb) {
  if (depthMeters <= 0.0 || frequencyHz <= 0.0) return 0.0;

  const double f = frequencyHz / 1e9;  // GHz
  const double fTerm = std::pow(f, 0.284);

  const double loss = (depthMeters <= 14.0)
                          ? 0.45 * fTerm * depthMeters
                          : 1.33 * fTerm * std::pow(depthMeters, 0.588);

  return std::clamp(loss, 0.0, maxLossDb);
}

}  // namespace rftrace::rf
