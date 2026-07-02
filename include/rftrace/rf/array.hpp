#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

#include <Eigen/Dense>

#include "rftrace/math.hpp"

// Antenna arrays and beam steering, Phase 7 / antenna-arrays.
//
// A phased array is a set of element positions (metres, in the array's local
// frame) driven by per-element complex weights. The array's far-field response
// toward a direction is the array factor
//
//   AF(u) = Σ_i w_i · exp( j·k · (p_i · û) ),   k = 2π/λ = 2π·f/c
//
// where û is the unit direction of the outgoing wave and p_i the i-th element
// position. Conjugate (matched) beam steering toward û₀ uses the weights
//
//   w_i = (1/√N) · conj( exp(j·k·p_i·û₀) ) = (1/√N) · exp(-j·k·p_i·û₀),
//
// which are power-normalised (Σ|w_i|² = 1). At the steering direction every
// term adds in phase, giving |AF(û₀)|² = N — the coherent-combining array gain
// of N over a single element (+10·log10(N) dB). Away from û₀ the terms
// dephase and |AF|² falls off, so the main beam is steered toward û₀.
//
// This is a narrowband, isotropic-element model; per-element pattern gain is
// carried separately as `elementGainDbi` and simply added to the array-factor
// term to yield the steered gain (dBi) fed into the link budget.

namespace rftrace::rf {

/// A phased antenna array: element positions in the array's local frame (m),
/// the operating frequency, and a per-element gain (dBi).
struct AntennaArray {
  std::vector<Vec3> elements;    ///< element positions (metres, local frame)
  double frequencyHz = 0.0;      ///< operating frequency (Hz)
  double elementGainDbi = 0.0;   ///< peak gain of a single element (dBi)
  /// Optional per-element boresight. Zero (default) = isotropic elements (front/
  /// back symmetric). When set, a cos² front-hemisphere element pattern is applied
  /// (floored behind boresight by `backLobeFloorDb`), giving a directional panel
  /// that suppresses the back lobe — the behaviour of a real sector antenna.
  Vec3 boresight{0.0, 0.0, 0.0};
  double backLobeFloorDb = -25.0;

  std::size_t size() const { return elements.size(); }

  /// Wavenumber k = 2π·f/c (rad/m). Zero for a non-positive frequency.
  double wavenumber() const {
    return frequencyHz > 0.0 ? constants::two_pi * frequencyHz / constants::c
                             : 0.0;
  }
};

/// Uniform linear array: `count` elements spaced `spacingMeters` apart along
/// `axis`, centred on the origin. Consecutive elements are exactly
/// `spacingMeters` apart. `axis` need not be normalised.
inline AntennaArray uniformLinearArray(std::size_t count, double spacingMeters,
                                       double frequencyHz,
                                       const Vec3& axis = Vec3(0.0, 1.0, 0.0),
                                       double elementGainDbi = 0.0) {
  AntennaArray arr;
  arr.frequencyHz = frequencyHz;
  arr.elementGainDbi = elementGainDbi;
  arr.elements.reserve(count);
  const double n = static_cast<double>(count);
  const Vec3 axn = axis.norm() > 0.0 ? axis.normalized() : Vec3(0.0, 1.0, 0.0);
  for (std::size_t i = 0; i < count; ++i) {
    const double offset = (static_cast<double>(i) - (n - 1.0) / 2.0) * spacingMeters;
    arr.elements.push_back(offset * axn);
  }
  return arr;
}

/// Uniform planar array: `nx`×`ny` grid with spacings `dx`/`dy` along `axisX`/
/// `axisY`, centred on the origin. Axes need not be normalised or orthogonal;
/// callers typically pass orthogonal axes (e.g. x and z).
inline AntennaArray uniformPlanarArray(std::size_t nx, std::size_t ny,
                                       double dx, double dy, double frequencyHz,
                                       const Vec3& axisX = Vec3(1.0, 0.0, 0.0),
                                       const Vec3& axisY = Vec3(0.0, 0.0, 1.0),
                                       double elementGainDbi = 0.0) {
  AntennaArray arr;
  arr.frequencyHz = frequencyHz;
  arr.elementGainDbi = elementGainDbi;
  arr.elements.reserve(nx * ny);
  const Vec3 ax = axisX.norm() > 0.0 ? axisX.normalized() : Vec3(1.0, 0.0, 0.0);
  const Vec3 ay = axisY.norm() > 0.0 ? axisY.normalized() : Vec3(0.0, 0.0, 1.0);
  const double cx = (static_cast<double>(nx) - 1.0) / 2.0;
  const double cy = (static_cast<double>(ny) - 1.0) / 2.0;
  for (std::size_t iy = 0; iy < ny; ++iy) {
    for (std::size_t ix = 0; ix < nx; ++ix) {
      const double ox = (static_cast<double>(ix) - cx) * dx;
      const double oy = (static_cast<double>(iy) - cy) * dy;
      arr.elements.push_back(ox * ax + oy * ay);
    }
  }
  return arr;
}

/// Complex array response (steering vector) a(û) with entries
/// exp(j·k·p_i·û) toward unit direction `dir` (normalised internally).
/// Returns a size-N vector; entries are 1 for a degenerate frequency/direction.
inline Eigen::VectorXcd arrayResponse(const AntennaArray& arr,
                                      const Vec3& dir) {
  const std::size_t n = arr.size();
  Eigen::VectorXcd a = Eigen::VectorXcd::Ones(static_cast<Eigen::Index>(n));
  const double k = arr.wavenumber();
  const double dn = dir.norm();
  if (k <= 0.0 || dn <= 0.0) return a;
  const Vec3 u = dir / dn;
  for (std::size_t i = 0; i < n; ++i) {
    const double phase = k * arr.elements[i].dot(u);
    a[static_cast<Eigen::Index>(i)] = std::polar(1.0, phase);
  }
  return a;
}

/// Matched (conjugate) beam-steering weights that point the main beam toward
/// `targetDir`. Power-normalised so Σ|w_i|² = 1; the coherent gain toward
/// `targetDir` is then |AF|² = N. Returns an empty vector for an empty array.
inline Eigen::VectorXcd steeringWeights(const AntennaArray& arr,
                                        const Vec3& targetDir) {
  const std::size_t n = arr.size();
  if (n == 0) return Eigen::VectorXcd();
  const Eigen::VectorXcd a = arrayResponse(arr, targetDir);
  return a.conjugate() / std::sqrt(static_cast<double>(n));
}

/// Complex array factor AF(û) = Σ_i w_i·exp(j·k·p_i·û) for the given `weights`
/// toward `dir`. `weights.size()` must equal `arr.size()`.
inline std::complex<double> arrayFactor(const AntennaArray& arr,
                                        const Eigen::VectorXcd& weights,
                                        const Vec3& dir) {
  const Eigen::VectorXcd a = arrayResponse(arr, dir);
  return (weights.array() * a.array()).sum();
}

/// Linear array-factor power |AF(û)|² toward `dir` for the given `weights`.
inline double arrayFactorPowerLinear(const AntennaArray& arr,
                                     const Eigen::VectorXcd& weights,
                                     const Vec3& dir) {
  return std::norm(arrayFactor(arr, weights, dir));
}

/// Total steered array gain (dBi) toward `dir`: the single-element gain plus the
/// array-factor term 10·log10(|AF|²). With matched, power-normalised weights the
/// peak (steering) direction yields elementGainDbi + 10·log10(N). This is the
/// value fed into the link-budget antenna term. Returns elementGainDbi when the
/// array factor is (numerically) zero, avoiding -inf.
/// Single-element gain (dBi) toward `dir`: `elementGainDbi` for isotropic elements
/// (zero boresight), otherwise a cos² front-hemisphere pattern floored behind the
/// boresight. Isotropic is the default, so existing arrays are unchanged.
inline double elementGainDb(const AntennaArray& arr, const Vec3& dir) {
  if (arr.boresight.squaredNorm() <= 0.0) return arr.elementGainDbi;
  const double dn = dir.norm();
  const double c = dn > 0.0 ? dir.dot(arr.boresight.normalized()) / dn : 0.0;
  const double front = c > 1e-3 ? std::max(arr.backLobeFloorDb, 20.0 * std::log10(c))
                                : arr.backLobeFloorDb;
  return arr.elementGainDbi + front;
}

inline double steeredGainDbi(const AntennaArray& arr,
                             const Eigen::VectorXcd& weights, const Vec3& dir) {
  const double elem = elementGainDb(arr, dir);
  const double p = arrayFactorPowerLinear(arr, weights, dir);
  if (p <= 0.0) return elem;
  return elem + 10.0 * std::log10(p);
}

/// Convenience: steered gain (dBi) toward `dir` for an array whose main beam is
/// steered toward `beamDir`. Combines `steeringWeights` and `steeredGainDbi`.
inline double steeredGainDbi(const AntennaArray& arr, const Vec3& beamDir,
                             const Vec3& dir) {
  return steeredGainDbi(arr, steeringWeights(arr, beamDir), dir);
}

}  // namespace rftrace::rf
