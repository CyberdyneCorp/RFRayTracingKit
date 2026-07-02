// Phase 7 golden regression: with DEFAULT settings (every Phase 7 feature flag
// off), the additive Phase 7 surface (extended SimulationSettings, the
// PropagationContext threaded through path building/finishing, and the new
// inert ReceiverResult fields) must leave results bit-for-bit identical to the
// archived Phase 1/2 behavior. The expected values below were captured from a
// run of the pre-Phase-7 engine; they must not drift as physics is added
// behind the (default-off) flags.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {

constexpr double kTol = 1e-9;  // dB / phase (rad)
constexpr double kDelayTol = 1e-15;  // seconds

Transmitter mkTx(const Vec3& p) {
  Transmitter t;
  t.id = "tx";
  t.position = p;
  t.frequencyHz = 3.5e9;
  t.powerDbm = 43.0;
  return t;
}
Receiver mkRx(const Vec3& p) {
  Receiver r;
  r.id = "rx";
  r.position = p;
  return r;
}

// A single path's four RF metrics against captured golden values.
void expectPath(const RFPath& p, double power, double loss, double phase,
                double delay) {
  EXPECT_NEAR(p.receivedPowerDbm, power, kTol);
  EXPECT_NEAR(p.pathLossDb, loss, kTol);
  EXPECT_NEAR(p.phaseRad, phase, kTol);
  EXPECT_NEAR(p.delaySeconds, delay, kDelayTol);
}

// The new SINR/serving-cell fields must stay inert under default settings.
void expectInertSinr(const ReceiverResult& rr) {
  EXPECT_TRUE(rr.servingTransmitterId.empty());
  EXPECT_TRUE(std::isnan(rr.sinrDb));
  EXPECT_TRUE(std::isnan(rr.interferencePowerDbm));
}

}  // namespace

// Every Phase 7 flag defaults to off / neutral.
TEST(Phase7Regression, DefaultsAreFeatureOff) {
  SimulationSettings s;
  EXPECT_FALSE(s.enableDiffraction);
  EXPECT_FALSE(s.enableRain);
  EXPECT_EQ(s.rainRateMmPerHr, 0.0);
  EXPECT_FALSE(s.enableGaseousAttenuation);
  EXPECT_FALSE(s.enableVegetation);
  EXPECT_FALSE(s.enableSinr);
  EXPECT_TRUE(std::isnan(s.noiseFloorDbmOverride));
}

// Golden 1: empty scene -> pure LOS.
TEST(Phase7Regression, EmptySceneLos) {
  Scene s;
  s.addTransmitter(mkTx({0, 0, 30}));
  s.addReceiver(mkRx({100, 0, 1.5}));

  SimulationSettings st;
  st.maxReflections = 2;
  const RFResult r = Simulator(st).run(s);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_EQ(rr->paths.size(), 1u);

  EXPECT_NEAR(rr->receivedPowerDbm, -40.668304897547586, kTol);
  EXPECT_NEAR(rr->pathLossDb, 83.668304897547586, kTol);
  EXPECT_NEAR(rr->phaseRad, 6.0494892838172021, kTol);
  expectPath(rr->paths[0], -40.668304897547586, 83.668304897547586,
             6.0494892838172021, 3.4684651603505745e-07);
  expectInertSinr(*rr);
}

// Golden 2: single wall -> LOS + one reflection.
TEST(Phase7Regression, SingleWallReflection) {
  Scene s;
  s.addMaterial(materials::preset("concrete"));
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  s.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  s.addTransmitter(mkTx({100, 20, 20}));
  s.addReceiver(mkRx({200, 20, 10}));

  SimulationSettings st;
  st.maxReflections = 1;
  const RFResult r = Simulator(st).run(s);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_EQ(rr->paths.size(), 2u);

  EXPECT_NEAR(rr->receivedPowerDbm, -40.236827136214266, kTol);
  EXPECT_NEAR(rr->pathLossDb, 83.372357846715289, kTol);
  EXPECT_NEAR(rr->delaySpreadNs, 50.912759033755194, kTol);
  expectPath(rr->paths[0], -40.372357846715289, 83.372357846715289,
             1.8672617942695595, 3.3522776684131559e-07);
  expectPath(rr->paths[1], -55.361882225263201, 98.361882225263201,
             5.5153645441460597, 6.3025079929432993e-07);
  expectInertSinr(*rr);
}

// Golden 3: two-building canyon -> LOS + a reflection off each wall.
TEST(Phase7Regression, TwoBuildingCanyon) {
  Scene s;
  s.addMaterial(materials::preset("concrete"));
  s.addMesh({Triangle{{0, 100, 0}, {300, 100, 0}, {300, 100, 50}},
             Triangle{{0, 100, 0}, {300, 100, 50}, {0, 100, 50}}},
            "concrete");
  s.addMesh({Triangle{{0, -100, 0}, {300, -100, 0}, {300, -100, 50}},
             Triangle{{0, -100, 0}, {300, -100, 50}, {0, -100, 50}}},
            "concrete");
  s.addTransmitter(mkTx({100, 0, 20}));
  s.addReceiver(mkRx({200, 0, 10}));

  SimulationSettings st;
  st.maxReflections = 1;
  const RFResult r = Simulator(st).run(s);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_EQ(rr->paths.size(), 3u);

  EXPECT_NEAR(rr->receivedPowerDbm, -40.157270324234666, kTol);
  EXPECT_NEAR(rr->delaySpreadNs, 88.218469790971, kTol);
  expectPath(rr->paths[0], -40.372357846715289, 83.372357846715289,
             1.8672617942695595, 3.3522776684131559e-07);
  expectPath(rr->paths[1], -56.32636114451148, 99.32636114451148,
             1.01296888750813, 7.4661749114447005e-07);
  expectPath(rr->paths[2], -56.32636114451148, 99.32636114451148,
             1.01296888750813, 7.4661749114447005e-07);
  expectInertSinr(*rr);
}

// Golden 4: a small coverage grid over an unobstructed scene.
TEST(Phase7Regression, CoverageGrid) {
  Scene s;
  s.addTransmitter(mkTx({50, 50, 30}));

  CoverageGrid g;
  g.origin = {0, 0, 0};
  g.cellSize = 25.0;
  g.cols = 4;
  g.rows = 4;
  g.height = 1.5;

  SimulationSettings st;
  st.maxReflections = 0;
  const CoverageResult cov = Simulator(st).runCoverage(s, g);

  const double expPower[16] = {
      -35.921924694194672, -34.085323072349183, -34.085323072349183,
      -35.921924694194672, -34.085323072349183, -30.839704127264923,
      -30.839704127264923, -34.085323072349183, -34.085323072349183,
      -30.839704127264923, -30.839704127264923, -34.085323072349183,
      -35.921924694194672, -34.085323072349183, -34.085323072349183,
      -35.921924694194672};
  const double expLoss[16] = {
      78.921924694194672, 77.085323072349183, 77.085323072349183,
      78.921924694194672, 77.085323072349183, 73.839704127264923,
      73.839704127264923, 77.085323072349183, 77.085323072349183,
      73.839704127264923, 73.839704127264923, 77.085323072349183,
      78.921924694194672, 77.085323072349183, 77.085323072349183,
      78.921924694194672};

  ASSERT_EQ(cov.powerDbm.size(), 16u);
  ASSERT_EQ(cov.pathLossDb.size(), 16u);
  for (int i = 0; i < 16; ++i) {
    EXPECT_NEAR(cov.powerDbm[i], expPower[i], kTol) << "cell " << i;
    EXPECT_NEAR(cov.pathLossDb[i], expLoss[i], kTol) << "cell " << i;
  }
}
