// MIMO channel matrix + capacity (Phase 7 / mimo-channel, R4): matrix shape,
// single-path rank-1 structure, and capacity growing with spatial richness.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <Eigen/Dense>

#include "rftrace/rf/array.hpp"
#include "rftrace/rf/mimo.hpp"
#include "rftrace/result.hpp"

using namespace rftrace;
using rftrace::rf::AntennaArray;
using rftrace::rf::capacity;
using rftrace::rf::channelMatrix;
using rftrace::rf::perStreamSinr;
using rftrace::rf::uniformLinearArray;

namespace {
constexpr double kFreqHz = 3.5e9;
double lambda() { return constants::c / kFreqHz; }

// A direction in the x-y plane at azimuth `deg` (0° = +x).
Vec3 azimuthDir(double deg) {
  const double r = deg * constants::pi / 180.0;
  return Vec3(std::cos(r), std::sin(r), 0.0);
}

// Build a straight two-point path whose departure and arrival directions both
// equal `dir`, with a given received power (dBm) and phase (rad).
RFPath straightPath(const Vec3& dir, double powerDbm, double phaseRad) {
  RFPath p;
  p.type = PathType::LOS;
  p.points = {Vec3::Zero(), dir.normalized()};
  p.receivedPowerDbm = powerDbm;
  p.phaseRad = phaseRad;
  return p;
}
}  // namespace

// H has shape n_rx × n_tx with complex entries.
TEST(Mimo, ChannelMatrixHasRxByTxShape) {
  const AntennaArray tx = uniformLinearArray(4, 0.5 * lambda(), kFreqHz);
  const AntennaArray rx = uniformLinearArray(2, 0.5 * lambda(), kFreqHz);

  const std::vector<RFPath> paths = {straightPath(azimuthDir(20.0), -60.0, 0.3)};
  const Eigen::MatrixXcd h = channelMatrix(paths, tx, rx);

  EXPECT_EQ(h.rows(), 2);
  EXPECT_EQ(h.cols(), 4);
}

// A single propagation path yields a rank-1 channel: one dominant eigenvalue,
// the rest (numerically) zero.
TEST(Mimo, SinglePathIsRankOne) {
  const AntennaArray tx = uniformLinearArray(4, 0.5 * lambda(), kFreqHz);
  const AntennaArray rx = uniformLinearArray(4, 0.5 * lambda(), kFreqHz);

  const std::vector<RFPath> paths = {straightPath(azimuthDir(35.0), -50.0, 1.1)};
  const Eigen::MatrixXcd h = channelMatrix(paths, tx, rx);

  const std::vector<double> sinr = perStreamSinr(h, 100.0);
  ASSERT_EQ(sinr.size(), 4u);
  // One dominant stream, the remainder negligible relative to it.
  EXPECT_GT(sinr[0], 0.0);
  for (std::size_t i = 1; i < sinr.size(); ++i)
    EXPECT_LT(sinr[i], sinr[0] * 1e-9);

  // Cross-check via Eigen's numerical rank.
  Eigen::FullPivLU<Eigen::MatrixXcd> lu(h);
  lu.setThreshold(1e-9);
  EXPECT_EQ(lu.rank(), 1);
}

// A well-conditioned multipath channel gives higher capacity than a rank-1
// channel at the same total SNR. Both matrices are normalised to equal
// Frobenius norm so the comparison is at equal total channel energy.
TEST(Mimo, MultipathCapacityExceedsRankOneAtEqualEnergy) {
  const AntennaArray tx = uniformLinearArray(4, 0.5 * lambda(), kFreqHz);
  const AntennaArray rx = uniformLinearArray(4, 0.5 * lambda(), kFreqHz);
  const double snr = 100.0;  // 20 dB

  // Rank-1: a single path.
  const std::vector<RFPath> single = {
      straightPath(azimuthDir(10.0), -55.0, 0.0)};
  Eigen::MatrixXcd h1 = channelMatrix(single, tx, rx);

  // Rich multipath: several well-separated angles → well-conditioned H.
  const std::vector<RFPath> multi = {
      straightPath(azimuthDir(-60.0), -55.0, 0.0),
      straightPath(azimuthDir(-20.0), -57.0, 1.7),
      straightPath(azimuthDir(25.0), -56.0, 3.0),
      straightPath(azimuthDir(65.0), -58.0, 4.5)};
  Eigen::MatrixXcd hm = channelMatrix(multi, tx, rx);

  // Normalise to equal Frobenius norm (equal total channel energy).
  h1 *= 1.0 / h1.norm();
  hm *= 1.0 / hm.norm();

  const double cap1 = capacity(h1, snr);
  const double capM = capacity(hm, snr);

  EXPECT_GT(capM, cap1);
}

// Degenerate inputs are handled gracefully.
TEST(Mimo, EmptyChannelAndSnrGuards) {
  const AntennaArray tx = uniformLinearArray(4, 0.5 * lambda(), kFreqHz);
  const AntennaArray rx = uniformLinearArray(4, 0.5 * lambda(), kFreqHz);

  // No paths → zero matrix, zero capacity.
  const Eigen::MatrixXcd h0 = channelMatrix(std::vector<RFPath>{}, tx, rx);
  EXPECT_EQ(h0.rows(), 4);
  EXPECT_EQ(h0.cols(), 4);
  EXPECT_DOUBLE_EQ(capacity(h0, 100.0), 0.0);

  const std::vector<RFPath> paths = {straightPath(azimuthDir(0.0), -50.0, 0.0)};
  const Eigen::MatrixXcd h = channelMatrix(paths, tx, rx);
  EXPECT_DOUBLE_EQ(capacity(h, 0.0), 0.0);  // non-positive SNR
  EXPECT_TRUE(perStreamSinr(h, -1.0).empty());
}
