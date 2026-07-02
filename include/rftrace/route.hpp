#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "rftrace/antenna.hpp"
#include "rftrace/math.hpp"

// Route (drive-test) simulation (Phase 7, D6). A route is an ordered polyline of
// waypoints sampled by arc length into receiver positions; each sample is
// evaluated as an ordinary point receiver and the per-sample metrics are
// collected in route order. This is additive and does not affect any existing
// point-receiver / coverage behavior.

namespace rftrace {

/// One sampled position along a route, with its arc-length distance from the
/// route start (metres).
struct RouteSamplePoint {
  Vec3 position{0, 0, 0};
  double distanceMeters = 0.0;
};

/// An ordered receiver route: a polyline of waypoints sampled into receiver
/// positions spaced ~`sampleSpacing` metres apart along the polyline.
struct Route {
  std::string id = "route";
  std::vector<Vec3> waypoints;  ///< ordered polyline vertices
  double sampleSpacing = 1.0;   ///< target arc-length spacing between samples (m)
  /// Receiver antenna / polarization applied at every sample (default omni).
  AntennaPattern antenna = AntennaPattern::Omnidirectional();
  Polarization polarization = Polarization::Vertical;

  /// Sample the polyline at ~`sampleSpacing` intervals. The first waypoint is
  /// always the first sample and the final waypoint is always the last sample;
  /// interior samples are spaced `sampleSpacing` apart. A degenerate route
  /// (0 waypoints, 1 waypoint, or zero total length) yields a single sample at
  /// the start (or none when empty).
  std::vector<RouteSamplePoint> sample() const {
    std::vector<RouteSamplePoint> out;
    if (waypoints.empty()) return out;
    if (waypoints.size() == 1) {
      out.push_back({waypoints.front(), 0.0});
      return out;
    }

    std::vector<double> cum(waypoints.size(), 0.0);
    for (std::size_t i = 1; i < waypoints.size(); ++i)
      cum[i] = cum[i - 1] + (waypoints[i] - waypoints[i - 1]).norm();
    const double total = cum.back();
    if (total <= 0.0) {
      out.push_back({waypoints.front(), 0.0});
      return out;
    }

    const double s = sampleSpacing > 0.0 ? sampleSpacing : total;
    const auto positionAt = [&](double d) -> Vec3 {
      std::size_t seg = 1;
      while (seg < cum.size() && cum[seg] < d) ++seg;
      if (seg >= cum.size()) return waypoints.back();
      const double segLen = cum[seg] - cum[seg - 1];
      const double t = segLen > 0.0 ? (d - cum[seg - 1]) / segLen : 0.0;
      return Vec3(waypoints[seg - 1] + t * (waypoints[seg] - waypoints[seg - 1]));
    };

    // Integer-indexed distances avoid floating-point accumulation drift.
    for (std::size_t i = 0; static_cast<double>(i) * s < total; ++i) {
      const double d = static_cast<double>(i) * s;
      out.push_back({positionAt(d), d});
    }
    // Always terminate on the exact endpoint (unless a sample already lands on
    // it, within a tiny tolerance).
    if (out.empty() || out.back().distanceMeters < total - 1e-9)
      out.push_back({waypoints.back(), total});
    return out;
  }
};

/// Per-sample result along a route (position + the usual RF metrics, plus
/// serving-cell / SINR when SINR is enabled). Ordered by route position.
struct RouteSample {
  int index = 0;                 ///< 0-based order along the route
  double distanceMeters = 0.0;   ///< arc-length from the route start
  Vec3 position{0, 0, 0};
  bool hasSignal = false;
  double receivedPowerDbm = 0.0;
  double pathLossDb = 0.0;
  double delaySpreadNs = 0.0;

  // Populated only when settings.enableSinr; inert otherwise.
  std::string servingTransmitterId;
  double sinrDb = std::numeric_limits<double>::quiet_NaN();
  double interferencePowerDbm = std::numeric_limits<double>::quiet_NaN();
};

/// Ordered series of per-sample results for a simulated route.
struct RouteResult {
  std::string routeId;
  std::string simulationId;
  double frequencyHz = 0.0;
  std::vector<RouteSample> samples;  ///< in route order
};

}  // namespace rftrace
