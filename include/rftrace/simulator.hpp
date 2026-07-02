#pragma once

#include <cstdint>
#include <string>

#include "rftrace/backend.hpp"
#include "rftrace/coverage.hpp"
#include "rftrace/result.hpp"
#include "rftrace/scene.hpp"

namespace rftrace {

/// Propagation method for point-receiver simulation.
///   ImageMethod — deterministic specular paths (exact; the correctness oracle).
///   RayLaunch   — stochastic Monte-Carlo ray launch with a receiver capture sphere.
enum class PropagationMode { ImageMethod, RayLaunch };

/// Knobs controlling a simulation run. Defaults form a runnable configuration
/// that preserves Phase 1 behavior (image method, point receivers).
struct SimulationSettings {
  Backend backend = Backend::CPU;
  PropagationMode mode = PropagationMode::ImageMethod;
  int maxReflections = 1;            ///< max specular bounces (0 = LOS only)
  int raysPerTransmitter = 100000;   ///< ray budget for RayLaunch mode
  double captureRadius = 2.0;        ///< receiver capture radius (m)
  double powerFloorDbm = -160.0;     ///< RayLaunch termination power floor
  std::uint64_t seed = 1;            ///< RNG/sampling seed (reproducibility)
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
  /// scene and settings (and seed, in ray-launch mode).
  RFResult run(const Scene& scene) const;

  /// Evaluate received power/path loss over a coverage grid, treating each cell
  /// centre as a receiver. Uses the deterministic image method per cell.
  CoverageResult runCoverage(const Scene& scene, const CoverageGrid& grid) const;

 private:
  SimulationSettings settings_;
};

}  // namespace rftrace
