#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "rftrace/backend.hpp"
#include "rftrace/coverage.hpp"
#include "rftrace/result.hpp"
#include "rftrace/route.hpp"
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

  // --- Phase 7 advanced RF (all additive and DEFAULT-OFF) -------------------
  // With every flag below at its default, the per-path budget, results, and
  // coverage are bit-for-bit identical to the archived Phase 1/2 behavior.
  // Physics for these hooks lands in later Phase 7 sub-changes.

  /// Enable single knife-edge (ITU-R P.526) diffraction over blocked LOS.
  bool enableDiffraction = false;

  /// Enable rain attenuation (ITU-R P.838); rate in mm/hr when enabled.
  bool enableRain = false;
  double rainRateMmPerHr = 0.0;

  /// Enable gaseous (oxygen/water-vapour) attenuation (ITU-R P.676 approx).
  bool enableGaseousAttenuation = false;

  /// Enable foliage attenuation along path depth through vegetation-tagged
  /// geometry (Weissberger / ITU-R P.833).
  bool enableVegetation = false;

  /// Enable SINR / serving-cell computation and SINR coverage.
  bool enableSinr = false;
  double noiseBandwidthHz = 20e6;  ///< receiver bandwidth B for kTB noise floor
  double noiseFigureDb = 7.0;      ///< receiver noise figure NF (dB)
  /// Optional fixed noise-floor override (dBm); NaN => derive from kTB + NF.
  double noiseFloorDbmOverride =
      std::numeric_limits<double>::quiet_NaN();
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

  /// Simulate a moving receiver along a route: sample the route's polyline into
  /// positions (spaced ~route.sampleSpacing), evaluate each as a point receiver
  /// against the scene's transmitters, and collect an ordered per-sample series
  /// (position + received power / path loss / delay spread, plus SINR when
  /// enabled). Deterministic in the same way as run().
  RouteResult runRoute(const Scene& scene, const Route& route) const;

 private:
  SimulationSettings settings_;
};

}  // namespace rftrace
