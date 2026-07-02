#pragma once

#include <utility>
#include <vector>

#include "rftrace/math.hpp"

namespace rftrace {

/// Wave polarization. `None` disables polarization-dependent effects.
enum class Polarization { Vertical, Horizontal, RHCP, LHCP, None };

/// Antenna radiation pattern returning gain (dBi) for a world-space direction.
///
/// The default is omnidirectional. A directional pattern is defined by a peak
/// gain, a boresight direction, an "up" reference, and an azimuth cut table of
/// (angle°, relative dB) samples that are linearly interpolated. This is enough
/// for Phase 1; richer 2D patterns arrive with the advanced-RF phase.
struct AntennaPattern {
  bool omni = true;
  double peakGainDbi = 0.0;
  Vec3 boresight{1.0, 0.0, 0.0};  ///< main-beam direction (world space)
  Vec3 up{0.0, 0.0, 1.0};         ///< reference up for azimuth measurement
  std::vector<std::pair<double, double>> azimuthCutDb;  ///< sorted by angle°

  static AntennaPattern Omnidirectional(double gainDbi = 0.0) {
    return AntennaPattern{true, gainDbi, {1, 0, 0}, {0, 0, 1}, {}};
  }

  /// Gain (dBi) toward a world-space direction (need not be normalized).
  double gainTowards(const Vec3& worldDir) const;
};

}  // namespace rftrace
