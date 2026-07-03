#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
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

/// Diffraction model used when enableDiffraction is set and a link is blocked.
///   SingleEdge — single dominant knife edge (ITU-R P.526); the default.
///   Bullington — multi-edge equivalent knife edge from a terrain profile.
///   Deygout    — multi-edge recursive dominant-edge from a terrain profile.
/// SingleEdge reproduces the archived behavior bit-for-bit.
enum class DiffractionModel { SingleEdge, Bullington, Deygout, UTD };

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

  /// Worker threads for the deterministic parallel-for over independent
  /// per-receiver / per-cell work (image-method run() and coverage). Additive
  /// and default-neutral:
  ///   0 or negative => std::thread::hardware_concurrency() (fallback 1);
  ///   1             => the exact pre-change serial code path (bit-for-bit);
  ///   N             => up to N workers over disjoint output slots.
  /// Results are bit-for-bit identical regardless of this value: iterations are
  /// independent, each writes only its own slot, and there is no cross-item
  /// floating-point reduction. Only effective on the CPU backend (whose queries
  /// are const and safe for concurrent reads); non-reentrant GPU backends always
  /// run the serial path.
  int threadCount = 0;

  // --- Phase 7 advanced RF (all additive and DEFAULT-OFF) -------------------
  // With every flag below at its default, the per-path budget, results, and
  // coverage are bit-for-bit identical to the archived Phase 1/2 behavior.
  // Physics for these hooks lands in later Phase 7 sub-changes.

  /// Enable single knife-edge (ITU-R P.526) diffraction over blocked LOS.
  bool enableDiffraction = false;

  /// Diffraction model applied to a blocked link when enableDiffraction is set.
  /// Default SingleEdge keeps the archived single dominant knife-edge behavior;
  /// Bullington / Deygout build a terrain profile and apply the multi-edge loss.
  DiffractionModel diffractionModel = DiffractionModel::SingleEdge;
  /// Depolarize the wave on reflection (apply per-bounce TE/TM Fresnel to the
  /// Jones state). Default off so reflected-path polarization mismatch stays 0 dB
  /// for co-polar links and archived results are unchanged.
  bool enableDepolarization = false;

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
  /// centre as a receiver. With mode == ImageMethod (the default) each cell is
  /// evaluated exactly per cell (LOS + specular reflections). With mode ==
  /// RayLaunch, rays are launched once per transmitter with each cell as a
  /// capture point (radius ≈ cellSize/2) and the captured LOS + reflected
  /// multipath is accumulated per cell; fully shadowed cells fall back to the
  /// knife-edge / multi-edge diffraction detour when enableDiffraction is set.
  CoverageResult runCoverage(const Scene& scene, const CoverageGrid& grid) const;

  /// Simulate a moving receiver along a route: sample the route's polyline into
  /// positions (spaced ~route.sampleSpacing), evaluate each as a point receiver
  /// against the scene's transmitters, and collect an ordered per-sample series
  /// (position + received power / path loss / delay spread, plus SINR when
  /// enabled). Deterministic in the same way as run().
  RouteResult runRoute(const Scene& scene, const Route& route) const;

  /// Number of times this Simulator has (re)built its acceleration backend.
  /// Repeated runs on an unchanged scene keep this at 1; it increments whenever
  /// the scene geometry changes and the backend is rebuilt. Observational only.
  std::size_t backendRebuildCount() const { return backendRebuildCount_; }

 private:
  /// Return the built acceleration backend for `scene`, reusing the cached one
  /// when the scene geometry is unchanged and rebuilding otherwise.
  ///
  /// The cache key is a content hash of the scene's triangle geometry plus the
  /// triangle count, so ANY geometry change — including an in-place edit that
  /// keeps the same triangle-vector address and size — invalidates the cache and
  /// triggers a rebuild. The backend KIND is fixed per Simulator via settings_,
  /// so it is deliberately NOT part of the key.
  ///
  /// Not thread-safe: the cache is touched exactly once, serially, at the top of
  /// each run method (before any parallel region), so it never interacts with
  /// in-run parallelism. Concurrent run/runCoverage/runRoute calls on ONE
  /// Simulator instance are unsupported, matching prior behavior. The cache holds
  /// the built structure for the Simulator's lifetime (traded for reuse).
  IBackend& ensureBackend(const Scene& scene) const;

  SimulationSettings settings_;

  mutable std::unique_ptr<IBackend> cachedBackend_;  ///< built acceleration structure
  mutable std::uint64_t cachedKey_ = 0;              ///< content hash of cached scene geometry
  mutable std::size_t backendRebuildCount_ = 0;      ///< # times build() was invoked
};

}  // namespace rftrace
