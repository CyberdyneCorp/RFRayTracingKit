#pragma once

#include <cmath>
#include <complex>
#include <limits>
#include <vector>

namespace rftrace::rf {

/// Received power (dBm) of a single path from its link-budget terms:
///   Prx = Ptx + Gtx + Grx − pathLoss
/// where `pathLossDb` already bundles FSPL + reflection + penetration losses.
inline double receivedPowerDbm(double ptxDbm, double gtxDbi, double grxDbi,
                               double pathLossDb) {
  return ptxDbm + gtxDbi + grxDbi - pathLossDb;
}

/// Incoherent (power-sum) aggregation of per-path powers:  10·log10(Σ 10^(Pi/10)).
/// Returns -inf for an empty set.
inline double aggregateIncoherentDbm(const std::vector<double>& powersDbm) {
  if (powersDbm.empty()) return -std::numeric_limits<double>::infinity();
  double linear = 0.0;
  for (double p : powersDbm) linear += std::pow(10.0, p / 10.0);
  return 10.0 * std::log10(linear);
}

/// Coherent aggregation: sum complex amplitudes using each path's phase so paths
/// can interfere.  P = 10·log10(|Σ √(10^(Pi/10))·e^{jφi}|²).
inline double aggregateCoherentDbm(const std::vector<double>& powersDbm,
                                   const std::vector<double>& phasesRad) {
  if (powersDbm.empty()) return -std::numeric_limits<double>::infinity();
  std::complex<double> sum{0.0, 0.0};
  for (std::size_t i = 0; i < powersDbm.size(); ++i) {
    const double amp = std::sqrt(std::pow(10.0, powersDbm[i] / 10.0));
    const double phase = i < phasesRad.size() ? phasesRad[i] : 0.0;
    sum += std::polar(amp, phase);
  }
  const double linear = std::norm(sum);  // |sum|^2
  if (linear <= 0.0) return -std::numeric_limits<double>::infinity();
  return 10.0 * std::log10(linear);
}

/// Power-weighted RMS delay spread (seconds) over multipath components.
inline double rmsDelaySpreadSeconds(const std::vector<double>& powersDbm,
                                    const std::vector<double>& delaysSeconds) {
  double wsum = 0.0, wtau = 0.0, wtau2 = 0.0;
  const std::size_t n = std::min(powersDbm.size(), delaysSeconds.size());
  for (std::size_t i = 0; i < n; ++i) {
    const double w = std::pow(10.0, powersDbm[i] / 10.0);
    wsum += w;
    wtau += w * delaysSeconds[i];
    wtau2 += w * delaysSeconds[i] * delaysSeconds[i];
  }
  if (wsum <= 0.0) return 0.0;
  const double mean = wtau / wsum;
  const double var = std::max(0.0, wtau2 / wsum - mean * mean);
  return std::sqrt(var);
}

}  // namespace rftrace::rf
