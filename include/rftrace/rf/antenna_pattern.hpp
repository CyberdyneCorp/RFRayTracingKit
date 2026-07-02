#pragma once

#include "rftrace/antenna.hpp"
#include "rftrace/math.hpp"

namespace rftrace::rf {

/// Transmitter/receiver antenna gain (dBi) toward a world-space direction.
/// Thin wrapper over AntennaPattern so RF link-budget code has a single entry.
inline double antennaGainDbi(const AntennaPattern& pattern, const Vec3& dir) {
  return pattern.gainTowards(dir);
}

}  // namespace rftrace::rf
