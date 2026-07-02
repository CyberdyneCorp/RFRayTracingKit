#pragma once

#include <cmath>

#include "rftrace/math.hpp"

namespace rftrace::rf {

/// Free-space path loss in dB:
///   FSPL = 20·log10(d) + 20·log10(f) + 20·log10(4π/c)
/// Returns 0 dB for a non-positive distance or frequency (co-located / invalid),
/// avoiding -inf/NaN.
inline double freeSpacePathLossDb(double distanceMeters, double frequencyHz) {
  if (distanceMeters <= 0.0 || frequencyHz <= 0.0) return 0.0;
  return 20.0 * std::log10(distanceMeters) + 20.0 * std::log10(frequencyHz) +
         20.0 * std::log10(4.0 * constants::pi / constants::c);
}

}  // namespace rftrace::rf
