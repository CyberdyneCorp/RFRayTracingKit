#pragma once

#include <cmath>

#include "rftrace/math.hpp"

namespace rftrace::rf {

/// Accumulated propagation phase (radians, wrapped to [0, 2π)) over a path of
/// length `pathLengthMeters`:  φ = (2π·f/c)·L.
inline double propagationPhaseRad(double pathLengthMeters, double frequencyHz) {
  const double phase =
      constants::two_pi * frequencyHz / constants::c * pathLengthMeters;
  double wrapped = std::fmod(phase, constants::two_pi);
  if (wrapped < 0.0) wrapped += constants::two_pi;
  return wrapped;
}

/// Propagation delay (seconds):  τ = L / c.
inline double propagationDelaySeconds(double pathLengthMeters) {
  return pathLengthMeters / constants::c;
}

}  // namespace rftrace::rf
