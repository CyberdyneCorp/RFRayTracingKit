#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

#include <Eigen/Dense>

#include "rftrace/math.hpp"
#include "rftrace/rf/array.hpp"
#include "rftrace/result.hpp"

// Narrowband MIMO channel matrix + capacity, Phase 7 / mimo-channel (R4).
//
// For a transmitter array of M elements and a receiver array of N elements, the
// geometric (ray-based) narrowband channel is the sum over propagation paths of
// a rank-1 outer product weighted by the path's complex gain:
//
//   H = Σ_p  g_p · a_rx(DoA_p) · a_tx(DoD_p)ᵀ      (N × M, complex)
//
// where a_tx / a_rx are the per-element array steering vectors (see array.hpp),
// DoD_p / DoA_p are the path's directions of departure / arrival, and g_p is the
// complex path gain built from the path's received power (amplitude) and its
// accumulated phase:  g_p = √(10^(Prx/10)) · e^{jφ}. A single path therefore
// contributes a single rank-1 term, so a one-path channel is rank 1; richer
// multipath adds independent outer products and raises the rank.
//
// The equal-power, narrowband capacity (bits/s/Hz) at a given linear SNR is the
// standard log-det formula
//
//   C = log2 det( I_N + (SNR/M) · H · Hᴴ ),   M = transmit elements,
//
// and the per-stream SINRs are (SNR/M)·λ_i for the eigenvalues λ_i of H·Hᴴ.
// Water-filling and wideband/subcarrier capacity are deferred to a follow-up.

namespace rftrace::rf {

/// Direction of departure of a path: the unit direction of its first segment
/// (tx → first point). Returns a zero vector for a degenerate path.
inline Vec3 directionOfDeparture(const RFPath& path) {
  if (path.points.size() < 2) return Vec3::Zero();
  const Vec3 d = path.points[1] - path.points.front();
  const double n = d.norm();
  return n > 0.0 ? Vec3(d / n) : Vec3::Zero();
}

/// Direction of arrival of a path: the unit direction of its last segment
/// (penultimate point → rx). Returns a zero vector for a degenerate path.
inline Vec3 directionOfArrival(const RFPath& path) {
  const std::size_t m = path.points.size();
  if (m < 2) return Vec3::Zero();
  const Vec3 d = path.points[m - 1] - path.points[m - 2];
  const double n = d.norm();
  return n > 0.0 ? Vec3(d / n) : Vec3::Zero();
}

/// Complex per-path gain g = √(10^(Prx/10))·e^{jφ}, with amplitude from the
/// path's received power (dBm → linear mW amplitude) and phase from its
/// accumulated propagation phase.
inline std::complex<double> pathComplexGain(const RFPath& path) {
  const double amp = std::sqrt(std::pow(10.0, path.receivedPowerDbm / 10.0));
  return std::polar(amp, path.phaseRad);
}

/// Assemble the MIMO channel matrix H (n_rx × n_tx, complex) from a set of
/// propagation `paths` and the tx/rx array geometries. Each path contributes a
/// rank-1 outer product g·a_rx(DoA)·a_tx(DoD)ᵀ. Degenerate paths (fewer than two
/// points, or a zero direction) are skipped.
inline Eigen::MatrixXcd channelMatrix(const std::vector<RFPath>& paths,
                                      const AntennaArray& txArray,
                                      const AntennaArray& rxArray) {
  const auto n = static_cast<Eigen::Index>(rxArray.size());
  const auto m = static_cast<Eigen::Index>(txArray.size());
  Eigen::MatrixXcd h = Eigen::MatrixXcd::Zero(n, m);
  if (n == 0 || m == 0) return h;
  for (const RFPath& path : paths) {
    const Vec3 dod = directionOfDeparture(path);
    const Vec3 doa = directionOfArrival(path);
    if (dod.isZero() || doa.isZero()) continue;
    const Eigen::VectorXcd aTx = arrayResponse(txArray, dod);
    const Eigen::VectorXcd aRx = arrayResponse(rxArray, doa);
    h += pathComplexGain(path) * (aRx * aTx.transpose());
  }
  return h;
}

/// Convenience overload: assemble H from a `ReceiverResult`'s paths.
inline Eigen::MatrixXcd channelMatrix(const ReceiverResult& receiver,
                                      const AntennaArray& txArray,
                                      const AntennaArray& rxArray) {
  return channelMatrix(receiver.paths, txArray, rxArray);
}

/// Narrowband equal-power MIMO capacity (bits/s/Hz):
///   C = log2 det( I + (SNR/M)·H·Hᴴ ),  M = transmit elements (H.cols()).
/// Returns 0 for an empty channel or non-positive SNR.
inline double capacity(const Eigen::MatrixXcd& h, double snrLinear) {
  const Eigen::Index m = h.cols();
  const Eigen::Index n = h.rows();
  if (m == 0 || n == 0 || snrLinear <= 0.0) return 0.0;
  const double scale = snrLinear / static_cast<double>(m);
  const Eigen::MatrixXcd gram =
      Eigen::MatrixXcd::Identity(n, n) + scale * (h * h.adjoint());
  const double det = gram.determinant().real();
  if (det <= 0.0) return 0.0;
  return std::log2(det);
}

/// Per-stream SINRs (SNR/M)·λ_i for the eigenvalues λ_i of H·Hᴴ, returned in
/// descending order. The dominant entry corresponds to the strongest spatial
/// stream; near-zero entries mark rank deficiency. Empty for an empty channel or
/// non-positive SNR.
inline std::vector<double> perStreamSinr(const Eigen::MatrixXcd& h,
                                         double snrLinear) {
  const Eigen::Index m = h.cols();
  const Eigen::Index n = h.rows();
  std::vector<double> sinr;
  if (m == 0 || n == 0 || snrLinear <= 0.0) return sinr;
  const double scale = snrLinear / static_cast<double>(m);
  const Eigen::MatrixXcd gram = h * h.adjoint();  // N × N, Hermitian PSD
  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXcd> solver(gram);
  const Eigen::VectorXd eig = solver.eigenvalues();  // ascending, real
  sinr.reserve(static_cast<std::size_t>(eig.size()));
  for (Eigen::Index i = eig.size() - 1; i >= 0; --i)
    sinr.push_back(scale * std::max(0.0, eig[i]));
  return sinr;
}

}  // namespace rftrace::rf
