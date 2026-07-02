#pragma once

#include <limits>
#include <string>
#include <vector>

#include "rftrace/math.hpp"
#include "rftrace/rf/polarization.hpp"

namespace rftrace {

enum class PathType { LOS, Reflection, Diffraction };

inline const char* toString(PathType type) {
  switch (type) {
    case PathType::LOS: return "los";
    case PathType::Reflection: return "reflection";
    case PathType::Diffraction: return "diffraction";
  }
  return "unknown";
}

/// A single propagation path from a transmitter to a receiver.
struct RFPath {
  std::string transmitterId;
  std::string receiverId;
  PathType type = PathType::LOS;
  std::vector<Vec3> points;       ///< tx, [bounces...], rx (in order)
  double receivedPowerDbm = 0.0;
  double pathLossDb = 0.0;
  double phaseRad = 0.0;
  double delaySeconds = 0.0;
  int reflections = 0;
  int diffractions = 0;
  std::vector<std::string> materialHits;  ///< materials touched, in order

  /// Doppler frequency shift (Hz) for this path (D4). Defaults to 0 for static
  /// receivers (point-receiver / coverage runs); the route simulator fills it
  /// from the derived receiver velocity and the path's arrival direction.
  double dopplerHz = 0.0;

  /// Tracked polarization state of the arriving wave (Jones vector). Defaults
  /// to the transmitter polarization (co-polar); `finishPath` sets it from the
  /// transmitter. Reflections may depolarize it via `rf::reflectDepolarize`.
  rf::Jones polarization;
};

/// Aggregated result for one receiver (across all paths from all transmitters).
struct ReceiverResult {
  std::string receiverId;
  Vec3 position{0, 0, 0};
  bool hasSignal = false;
  double receivedPowerDbm = 0.0;
  double pathLossDb = 0.0;
  double delaySpreadNs = 0.0;
  double phaseRad = 0.0;
  std::vector<RFPath> paths;

  // --- Phase 7 SINR / serving-cell (populated only when enableSinr) ---------
  // Inert defaults leave archived results unchanged. `servingTransmitterId`
  // stays empty and the metrics stay NaN until SINR is computed.
  std::string servingTransmitterId;
  double sinrDb = std::numeric_limits<double>::quiet_NaN();
  double interferencePowerDbm = std::numeric_limits<double>::quiet_NaN();
};

/// Lightweight transmitter record carried into results for export.
struct TransmitterInfo {
  std::string id;
  Vec3 position{0, 0, 0};
  double frequencyHz = 0.0;
  double powerDbm = 0.0;
};

/// The full simulation output.
struct RFResult {
  std::string simulationId;
  double frequencyHz = 0.0;
  std::vector<TransmitterInfo> transmitters;
  std::vector<ReceiverResult> receivers;

  const ReceiverResult* receiver(const std::string& id) const {
    for (const auto& r : receivers)
      if (r.receiverId == id) return &r;
    return nullptr;
  }
};

}  // namespace rftrace
