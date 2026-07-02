#pragma once

#include <string>

#include "rftrace/backend.hpp"
#include "rftrace/result.hpp"
#include "rftrace/scene.hpp"

namespace rftrace {

/// Knobs controlling a simulation run. Defaults form a runnable configuration.
struct SimulationSettings {
  Backend backend = Backend::CPU;
  int maxReflections = 1;            ///< max specular bounces (0 = LOS only)
  int raysPerTransmitter = 500000;   ///< reserved for stochastic modes
  double captureRadius = 1.0;        ///< receiver capture radius (m), reserved
  bool coherent = false;             ///< coherent vs incoherent power combining
  bool allowBackendFallback = true;  ///< fall back to CPU if backend absent
  std::string simulationId = "rftrace_sim";
};

/// Runs an RF propagation simulation over a scene, producing per-receiver
/// results. Point-receiver mode (LOS + specular reflections via the image
/// method) is the Phase 1 capability.
class Simulator {
 public:
  explicit Simulator(SimulationSettings settings = {})
      : settings_(std::move(settings)) {}

  const SimulationSettings& settings() const { return settings_; }

  /// Simulate every (transmitter, receiver) pair. Deterministic for a fixed
  /// scene and settings.
  RFResult run(const Scene& scene) const;

 private:
  SimulationSettings settings_;
};

}  // namespace rftrace
