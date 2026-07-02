#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "rftrace/math.hpp"
#include "rftrace/rf/channel.hpp"
#include "rftrace/rf/free_space_path_loss.hpp"
#include "rftrace/rf/fresnel.hpp"
#include "rftrace/rf/phase.hpp"

using namespace rftrace;
using namespace rftrace::rf;

TEST(RF, FsplReferenceValue) {
  // 1 km at 3.5 GHz ≈ 103.3 dB.
  EXPECT_NEAR(freeSpacePathLossDb(1000.0, 3.5e9), 103.3, 0.15);
}

TEST(RF, FsplScalingIs6dBPerDoubling) {
  const double base = freeSpacePathLossDb(1000.0, 3.5e9);
  EXPECT_NEAR(freeSpacePathLossDb(2000.0, 3.5e9) - base, 6.02, 0.02);
  EXPECT_NEAR(freeSpacePathLossDb(1000.0, 7.0e9) - base, 6.02, 0.02);
}

TEST(RF, FsplGuardsZeroDistance) {
  EXPECT_TRUE(std::isfinite(freeSpacePathLossDb(0.0, 3.5e9)));
  EXPECT_EQ(freeSpacePathLossDb(0.0, 3.5e9), 0.0);
}

TEST(RF, FresnelPecLimitApproachesUnity) {
  // Very high conductivity -> perfect conductor -> |Γ| ≈ 1.
  const Complex epsc = complexPermittivity(1.0, 1.0e7, 1.0e9);
  const double incidence = 30.0 * constants::pi / 180.0;
  EXPECT_NEAR(reflectionCoefficientMagnitude(epsc, incidence, FresnelPolarization::TE), 1.0, 1e-3);
  EXPECT_NEAR(reflectionCoefficientMagnitude(epsc, incidence, FresnelPolarization::TM), 1.0, 1e-3);
}

TEST(RF, FresnelPolarizationsDiffer) {
  const Complex epsc = complexPermittivity(5.31, 0.0326, 3.5e9);
  const double incidence = 60.0 * constants::pi / 180.0;
  const double te = reflectionCoefficientMagnitude(epsc, incidence, FresnelPolarization::TE);
  const double tm = reflectionCoefficientMagnitude(epsc, incidence, FresnelPolarization::TM);
  EXPECT_GT(std::abs(te - tm), 1e-3);
}

TEST(RF, PhaseForOneWavelengthIsFullCycle) {
  const double f = 3.5e9;
  const double lambda = constants::c / f;
  const double p = propagationPhaseRad(lambda, f);
  // 2π mod 2π ≈ 0; accept either extreme.
  const double dist = std::min(p, constants::two_pi - p);
  EXPECT_NEAR(dist, 0.0, 1e-6);
}

TEST(RF, DelayForKnownLength) {
  EXPECT_NEAR(propagationDelaySeconds(300.0), 1.0006922e-6, 1e-9);
}

TEST(RF, IncoherentAggregationOfEqualPaths) {
  // Two equal powers combine to +3.01 dB.
  const std::vector<double> powers = {-70.0, -70.0};
  EXPECT_NEAR(aggregateIncoherentDbm(powers), -70.0 + 3.0103, 1e-3);
}

TEST(RF, CoherentAggregationInterferes) {
  const std::vector<double> powers = {-70.0, -70.0};
  // In phase: +6.02 dB.
  EXPECT_NEAR(aggregateCoherentDbm(powers, {0.0, 0.0}), -70.0 + 6.0206, 1e-3);
  // Anti-phase: cancellation -> very low power.
  EXPECT_LT(aggregateCoherentDbm(powers, {0.0, constants::pi}), -200.0);
}

TEST(RF, RmsDelaySpreadZeroForSinglePath) {
  EXPECT_NEAR(rmsDelaySpreadSeconds({-70.0}, {1e-6}), 0.0, 1e-12);
}
