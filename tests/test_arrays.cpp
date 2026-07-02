// Antenna arrays + beam steering (Phase 7 / antenna-arrays): element geometry,
// the array factor, matched beam steering, and the steered gain (dBi) that feeds
// the link budget.

#include <gtest/gtest.h>

#include <cmath>

#include "rftrace/rf/array.hpp"
#include "rftrace/rf/free_space_path_loss.hpp"

using namespace rftrace;
using rftrace::rf::AntennaArray;
using rftrace::rf::arrayFactorPowerLinear;
using rftrace::rf::steeredGainDbi;
using rftrace::rf::steeringWeights;
using rftrace::rf::uniformLinearArray;
using rftrace::rf::uniformPlanarArray;

namespace {
constexpr double kFreqHz = 3.5e9;
double lambda() { return constants::c / kFreqHz; }

// A direction in the x-y plane at azimuth `deg` (0° = +x). A ULA on the y axis
// senses only the y-component of a direction, so sweeping azimuth here moves
// through the array's main beam.
Vec3 azimuthDir(double deg) {
  const double r = deg * constants::pi / 180.0;
  return Vec3(std::cos(r), std::sin(r), 0.0);
}
}  // namespace

// A ULA of N elements has exactly N positions with uniform spacing d between
// consecutive elements, and is centred on the origin.
TEST(Arrays, UlaGeometryHasNPositionsAtSpacingD) {
  const std::size_t n = 8;
  const double d = 0.5 * lambda();
  const AntennaArray ula = uniformLinearArray(n, d, kFreqHz);

  ASSERT_EQ(ula.size(), n);
  for (std::size_t i = 1; i < n; ++i) {
    const double gap = (ula.elements[i] - ula.elements[i - 1]).norm();
    EXPECT_NEAR(gap, d, 1e-12) << "between elements " << i - 1 << " and " << i;
  }
  // Centred: the element positions sum to (approximately) the origin.
  Vec3 centroid = Vec3::Zero();
  for (const Vec3& p : ula.elements) centroid += p;
  centroid /= static_cast<double>(n);
  EXPECT_NEAR(centroid.norm(), 0.0, 1e-12);
}

// A UPA of nx*ny elements has nx*ny positions.
TEST(Arrays, UpaHasNxTimesNyPositions) {
  const AntennaArray upa = uniformPlanarArray(4, 3, 0.5 * lambda(),
                                              0.5 * lambda(), kFreqHz);
  EXPECT_EQ(upa.size(), 12u);
}

// The steered beam is maximal at the target direction and lower away from it.
TEST(Arrays, SteeredBeamPeaksAtTargetAndRollsOff) {
  const std::size_t n = 8;
  const AntennaArray ula = uniformLinearArray(n, 0.5 * lambda(), kFreqHz);

  const double targetDeg = 30.0;
  const Vec3 target = azimuthDir(targetDeg);
  const Eigen::VectorXcd w = steeringWeights(ula, target);

  const double peak = arrayFactorPowerLinear(ula, w, target);

  // Peak equals the coherent-combining gain N and is the global maximum over a
  // fine azimuth sweep.
  EXPECT_NEAR(peak, static_cast<double>(n), 1e-9);
  for (double deg = -90.0; deg <= 90.0; deg += 1.0) {
    const double p = arrayFactorPowerLinear(ula, w, azimuthDir(deg));
    EXPECT_LE(p, peak + 1e-9) << "az=" << deg;
  }

  // Strictly lower as the observation direction moves off the beam (staying
  // inside the main lobe so the fall-off is monotonic).
  const double offClose = arrayFactorPowerLinear(ula, w, azimuthDir(40.0));
  const double offFar = arrayFactorPowerLinear(ula, w, azimuthDir(60.0));
  EXPECT_LT(offClose, peak);
  EXPECT_LT(offFar, offClose);
}

// The steered array gain toward the beam exceeds a single element by the
// coherent-combining gain 10·log10(N).
TEST(Arrays, SteeredGainExceedsSingleElement) {
  const std::size_t n = 8;
  const double elemGain = 3.0;  // dBi per element
  const AntennaArray ula =
      uniformLinearArray(n, 0.5 * lambda(), kFreqHz, Vec3(0, 1, 0), elemGain);

  const Vec3 target = azimuthDir(-20.0);
  const double gain = steeredGainDbi(ula, target, target);

  EXPECT_GT(gain, elemGain);
  EXPECT_NEAR(gain, elemGain + 10.0 * std::log10(static_cast<double>(n)), 1e-9);
}

// The steered gain drops into the link budget like any other antenna term:
// received power with the steered array beats a single element on the same path.
TEST(Arrays, SteeredGainInPathBudget) {
  const std::size_t n = 16;
  const AntennaArray ula = uniformLinearArray(n, 0.5 * lambda(), kFreqHz);

  const Vec3 target = azimuthDir(10.0);
  const double gArray = steeredGainDbi(ula, target, target);
  const double gSingle = ula.elementGainDbi;  // 0 dBi

  const double ptxDbm = 43.0;
  const double grxDbi = 2.0;
  const double fspl = rf::freeSpacePathLossDb(500.0, kFreqHz);

  const double prxArray = ptxDbm + gArray + grxDbi - fspl;
  const double prxSingle = ptxDbm + gSingle + grxDbi - fspl;

  EXPECT_GT(prxArray, prxSingle);
  EXPECT_NEAR(prxArray - prxSingle, 10.0 * std::log10(static_cast<double>(n)),
              1e-9);
}
