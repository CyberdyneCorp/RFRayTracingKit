#include "rftrace/antenna.hpp"

#include <algorithm>
#include <cmath>

namespace rftrace {

namespace {
/// Linear interpolation over a table of (angleDeg, valueDb) sorted by angle.
double interpolateCut(const std::vector<std::pair<double, double>>& table,
                      double angleDeg) {
  if (table.empty()) return 0.0;
  if (angleDeg <= table.front().first) return table.front().second;
  if (angleDeg >= table.back().first) return table.back().second;
  for (std::size_t i = 1; i < table.size(); ++i) {
    if (angleDeg <= table[i].first) {
      const auto& a = table[i - 1];
      const auto& b = table[i];
      const double f = (angleDeg - a.first) / (b.first - a.first);
      return a.second + f * (b.second - a.second);
    }
  }
  return table.back().second;
}
}  // namespace

double AntennaPattern::gainTowards(const Vec3& worldDir) const {
  if (omni) return peakGainDbi;

  const double dirNorm = worldDir.norm();
  if (dirNorm <= 0.0) return peakGainDbi;
  const Vec3 dir = worldDir / dirNorm;

  // Azimuth measured in the plane perpendicular to `up`, from the boresight.
  const Vec3 upn = up.normalized();
  const Vec3 boreProj = (boresight - boresight.dot(upn) * upn).normalized();
  const Vec3 dirProj = dir - dir.dot(upn) * upn;
  const double projNorm = dirProj.norm();
  double azimuthDeg = 0.0;
  if (projNorm > 1e-9) {
    const Vec3 dpn = dirProj / projNorm;
    const double cosA = std::clamp(boreProj.dot(dpn), -1.0, 1.0);
    azimuthDeg = std::acos(cosA) * 180.0 / constants::pi;
  }
  return peakGainDbi + interpolateCut(azimuthCutDb, azimuthDeg);
}

}  // namespace rftrace
