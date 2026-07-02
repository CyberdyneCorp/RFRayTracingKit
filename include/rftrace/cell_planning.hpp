#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "rftrace/result.hpp"
#include "rftrace/rf/channel.hpp"
#include "rftrace/simulator.hpp"

// Cell planning: per-receiver serving-cell selection and SINR (Phase 7, R3).
//
// Each receiver may be reached by several transmitters. The serving cell is the
// transmitter with the highest received power; every other transmitter that
// reaches the receiver is an interferer. SINR is
//
//   SINR = P_serving / ( Σ P_interferer + N )   (linear), reported in dB,
//
// where the noise floor defaults to the physical thermal value
//
//   N = k·T·B · 10^(NF/10)   →   N[dBm] = 10·log10(k·T·B) + 30 + NF
//                               ≈ −174 dBm/Hz + 10·log10(B) + NF,
//
// with k Boltzmann's constant, T = 290 K, B the receiver bandwidth (Hz) and NF
// the receiver noise figure (dB). A fixed-dBm override may replace it. With a
// single transmitter the interference term is zero, so SINR reduces to SNR.
//
// All functions here are pure and header-only (mirroring rf/*.hpp); they run
// only when settings.enableSinr is set, so default runs are unaffected.

namespace rftrace {

/// Physical thermal noise floor (dBm) for a receiver of bandwidth `bandwidthHz`
/// and noise figure `noiseFigureDb`, at the standard reference temperature
/// T = 290 K. Equals 10·log10(k·T·B) + 30 + NF ≈ −174 + 10·log10(B) + NF.
/// Returns -inf for a non-positive bandwidth.
inline double thermalNoiseFloorDbm(double bandwidthHz, double noiseFigureDb) {
  constexpr double kBoltzmann = 1.380649e-23;  // J/K
  constexpr double kRefTempK = 290.0;          // K
  if (bandwidthHz <= 0.0) return -std::numeric_limits<double>::infinity();
  const double noiseWatts = kBoltzmann * kRefTempK * bandwidthHz;
  return 10.0 * std::log10(noiseWatts) + 30.0 /* W→mW */ + noiseFigureDb;
}

/// Effective noise floor (dBm) honoring the optional fixed-dBm override:
/// returns `noiseFloorDbmOverride` when finite, else the derived thermal value.
inline double effectiveNoiseFloorDbm(const SimulationSettings& settings) {
  if (std::isfinite(settings.noiseFloorDbmOverride))
    return settings.noiseFloorDbmOverride;
  return thermalNoiseFloorDbm(settings.noiseBandwidthHz, settings.noiseFigureDb);
}

/// Serving-cell + SINR outcome for one receiver.
struct SinrOutcome {
  bool served = false;  ///< false when no transmitter reaches the receiver
  std::string servingTransmitterId;
  double servingPowerDbm = -std::numeric_limits<double>::infinity();
  /// Aggregate interference power (dBm); -inf when there are no interferers.
  double interferencePowerDbm = -std::numeric_limits<double>::infinity();
  double sinrDb = std::numeric_limits<double>::quiet_NaN();
};

/// Aggregate a receiver's paths per transmitter, pick the strongest as the
/// serving cell, and compute SINR against the summed interferers plus the noise
/// floor. Path powers are combined coherently or incoherently per
/// `settings.coherent`, matching the per-receiver aggregation. Returns an
/// unserved outcome when the receiver has no paths.
inline SinrOutcome computeSinr(const ReceiverResult& receiver,
                               const SimulationSettings& settings) {
  SinrOutcome out;
  if (receiver.paths.empty()) return out;

  // Group per-transmitter path powers (and phases) in first-seen order so the
  // result is deterministic and ties resolve to the earliest transmitter.
  std::vector<std::string> ids;
  std::vector<std::vector<double>> powersDbm;
  std::vector<std::vector<double>> phasesRad;
  const auto indexOf = [&](const std::string& id) -> std::size_t {
    for (std::size_t i = 0; i < ids.size(); ++i)
      if (ids[i] == id) return i;
    ids.push_back(id);
    powersDbm.emplace_back();
    phasesRad.emplace_back();
    return ids.size() - 1;
  };
  for (const RFPath& p : receiver.paths) {
    const std::size_t k = indexOf(p.transmitterId);
    powersDbm[k].push_back(p.receivedPowerDbm);
    phasesRad[k].push_back(p.phaseRad);
  }

  // Per-transmitter received power, aggregated like the receiver summary.
  std::vector<double> txPowerDbm(ids.size());
  for (std::size_t i = 0; i < ids.size(); ++i)
    txPowerDbm[i] = settings.coherent
                        ? rf::aggregateCoherentDbm(powersDbm[i], phasesRad[i])
                        : rf::aggregateIncoherentDbm(powersDbm[i]);

  std::size_t serving = 0;
  for (std::size_t i = 1; i < ids.size(); ++i)
    if (txPowerDbm[i] > txPowerDbm[serving]) serving = i;

  const auto toLinear = [](double dbm) { return std::pow(10.0, dbm / 10.0); };
  double interferenceLinear = 0.0;
  for (std::size_t i = 0; i < ids.size(); ++i)
    if (i != serving) interferenceLinear += toLinear(txPowerDbm[i]);

  out.served = true;
  out.servingTransmitterId = ids[serving];
  out.servingPowerDbm = txPowerDbm[serving];
  out.interferencePowerDbm =
      interferenceLinear > 0.0 ? 10.0 * std::log10(interferenceLinear)
                               : -std::numeric_limits<double>::infinity();

  const double noiseLinear = toLinear(effectiveNoiseFloorDbm(settings));
  const double denom = interferenceLinear + noiseLinear;
  out.sinrDb = 10.0 * std::log10(toLinear(out.servingPowerDbm) / denom);
  return out;
}

/// Populate a receiver's SINR / serving-cell fields in place. No-op for an
/// unreached receiver, leaving the inert defaults untouched.
inline void applySinr(ReceiverResult& receiver,
                      const SimulationSettings& settings) {
  if (!receiver.hasSignal) return;
  const SinrOutcome o = computeSinr(receiver, settings);
  if (!o.served) return;
  receiver.servingTransmitterId = o.servingTransmitterId;
  receiver.sinrDb = o.sinrDb;
  receiver.interferencePowerDbm = o.interferencePowerDbm;
}

}  // namespace rftrace
