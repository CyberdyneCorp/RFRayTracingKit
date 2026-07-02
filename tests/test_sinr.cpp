#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "rftrace/cell_planning.hpp"
#include "rftrace/coverage.hpp"
#include "rftrace/exporters/csv_exporter.hpp"
#include "rftrace/exporters/json_exporter.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {

// Open-field scene (no geometry) so every transmitter reaches the receiver over
// pure LOS. Transmitter powers/positions are chosen by the caller.
Scene twoTxScene(double txPowerDbmA, double txPowerDbmB) {
  Scene scene;
  Transmitter a;
  a.id = "txA";
  a.position = {0, 0, 10};
  a.frequencyHz = 3.5e9;
  a.powerDbm = txPowerDbmA;
  scene.addTransmitter(a);

  Transmitter b;
  b.id = "txB";
  b.position = {200, 0, 10};
  b.frequencyHz = 3.5e9;
  b.powerDbm = txPowerDbmB;
  scene.addTransmitter(b);

  Receiver rx;
  rx.id = "rx";
  rx.position = {100, 0, 10};  // equidistant from both transmitters
  scene.addReceiver(rx);
  return scene;
}

SimulationSettings sinrSettings() {
  SimulationSettings s;
  s.maxReflections = 0;  // LOS only, deterministic
  s.enableSinr = true;
  s.noiseBandwidthHz = 20e6;
  s.noiseFigureDb = 7.0;
  return s;
}

double toLinear(double dbm) { return std::pow(10.0, dbm / 10.0); }

}  // namespace

// --- Noise floor -------------------------------------------------------------

TEST(Sinr, NoiseFloorMatchesMinus174Formula) {
  const double b = 20e6, nf = 7.0;
  const double expected = -174.0 + 10.0 * std::log10(b) + nf;
  EXPECT_NEAR(thermalNoiseFloorDbm(b, nf), expected, 0.05);
}

TEST(Sinr, NoiseFloorOverrideWins) {
  SimulationSettings s;
  s.noiseFloorDbmOverride = -95.0;
  EXPECT_DOUBLE_EQ(effectiveNoiseFloorDbm(s), -95.0);
}

TEST(Sinr, NoiseFloorDerivedWhenNoOverride) {
  SimulationSettings s;
  s.noiseBandwidthHz = 20e6;
  s.noiseFigureDb = 7.0;
  EXPECT_NEAR(effectiveNoiseFloorDbm(s),
              thermalNoiseFloorDbm(20e6, 7.0), 1e-12);
}

// --- Single transmitter reduces to SNR --------------------------------------

TEST(Sinr, SingleTransmitterEqualsSnr) {
  Scene scene;
  Transmitter tx;
  tx.id = "tx";
  tx.position = {0, 0, 10};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);
  Receiver rx;
  rx.id = "rx";
  rx.position = {100, 0, 10};
  scene.addReceiver(rx);

  const SimulationSettings s = sinrSettings();
  const RFResult r = Simulator(s).run(scene);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_TRUE(rr->hasSignal);

  EXPECT_EQ(rr->servingTransmitterId, "tx");
  EXPECT_FALSE(std::isfinite(rr->interferencePowerDbm));  // no interferers

  const double snr = rr->receivedPowerDbm - effectiveNoiseFloorDbm(s);
  EXPECT_NEAR(rr->sinrDb, snr, 1e-9);
}

// --- SINR = S / (I + N) ------------------------------------------------------

TEST(Sinr, CombinesSignalInterferenceAndNoise) {
  const SimulationSettings s = sinrSettings();
  const RFResult r = Simulator(s).run(twoTxScene(43.0, 30.0));
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_TRUE(rr->hasSignal);

  // Two equidistant transmitters => per-tx power differs only by tx power.
  ASSERT_EQ(rr->paths.size(), 2u);
  double serving = -1e300, interferer = -1e300;
  for (const RFPath& p : rr->paths) {
    if (p.transmitterId == "txA") serving = p.receivedPowerDbm;
    else interferer = p.receivedPowerDbm;
  }
  const double noise = effectiveNoiseFloorDbm(s);
  const double expected =
      10.0 * std::log10(toLinear(serving) /
                        (toLinear(interferer) + toLinear(noise)));

  EXPECT_EQ(rr->servingTransmitterId, "txA");  // higher power serves
  EXPECT_NEAR(rr->interferencePowerDbm, interferer, 1e-9);
  EXPECT_NEAR(rr->sinrDb, expected, 1e-9);
}

TEST(Sinr, StrongestTransmitterServes) {
  const SimulationSettings s = sinrSettings();
  // txB is stronger here; it must become the serving cell.
  const RFResult r = Simulator(s).run(twoTxScene(20.0, 43.0));
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  EXPECT_EQ(rr->servingTransmitterId, "txB");
}

TEST(Sinr, DisabledLeavesFieldsInert) {
  SimulationSettings s;
  s.maxReflections = 0;  // enableSinr stays false
  const RFResult r = Simulator(s).run(twoTxScene(43.0, 30.0));
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  EXPECT_TRUE(rr->servingTransmitterId.empty());
  EXPECT_TRUE(std::isnan(rr->sinrDb));
  EXPECT_TRUE(std::isnan(rr->interferencePowerDbm));
}

// --- Export ------------------------------------------------------------------

TEST(Sinr, CsvCarriesServingAndSinrWhenPresent) {
  const SimulationSettings s = sinrSettings();
  const RFResult r = Simulator(s).run(twoTxScene(43.0, 30.0));
  const std::string csv = io::receiversToCsvString(r);
  EXPECT_NE(csv.find("serving_transmitter_id"), std::string::npos);
  EXPECT_NE(csv.find("sinr_db"), std::string::npos);
  EXPECT_NE(csv.find("txA"), std::string::npos);
}

TEST(Sinr, CsvOmitsSinrColumnsWhenDisabled) {
  SimulationSettings s;
  s.maxReflections = 0;
  const RFResult r = Simulator(s).run(twoTxScene(43.0, 30.0));
  const std::string csv = io::receiversToCsvString(r);
  EXPECT_EQ(csv.find("sinr_db"), std::string::npos);
}

TEST(Sinr, JsonCarriesServingAndSinr) {
  const SimulationSettings s = sinrSettings();
  const RFResult r = Simulator(s).run(twoTxScene(43.0, 30.0));
  const std::string j = io::resultToJsonString(r);
  EXPECT_NE(j.find("serving_transmitter_id"), std::string::npos);
  EXPECT_NE(j.find("sinr_db"), std::string::npos);

  // Round-trips back through the loader.
  const RFResult back = io::resultFromJsonString(j);
  const ReceiverResult* rr = back.receiver("rx");
  ASSERT_NE(rr, nullptr);
  EXPECT_EQ(rr->servingTransmitterId, "txA");
  EXPECT_TRUE(std::isfinite(rr->sinrDb));
}

// --- SINR coverage -----------------------------------------------------------

TEST(Sinr, CoverageArrayAlignsToGridWithSentinel) {
  Scene scene;
  Transmitter a;
  a.id = "txA";
  a.position = {0, 0, 20};
  a.frequencyHz = 3.5e9;
  a.powerDbm = 43.0;
  scene.addTransmitter(a);
  Transmitter b;
  b.id = "txB";
  b.position = {80, 80, 20};
  b.frequencyHz = 3.5e9;
  b.powerDbm = 43.0;
  scene.addTransmitter(b);

  CoverageGrid g;
  g.cellSize = 20.0;
  g.cols = 4;
  g.rows = 4;

  const SimulationSettings s = sinrSettings();
  const CoverageResult cov = Simulator(s).runCoverage(scene, g);

  ASSERT_EQ(static_cast<int>(cov.sinrDb.size()), g.cellCount());
  // Open field: every cell is reached, so SINR is finite everywhere.
  for (int i = 0; i < g.cellCount(); ++i) {
    EXPECT_NE(cov.powerDbm[i], CoverageResult::NoSignal);
    EXPECT_TRUE(std::isfinite(cov.sinrDb[i])) << "cell " << i;
  }
}

TEST(Sinr, CoverageSentinelWhereUnreached) {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  // A tall wall that shadows part of the grid from the only transmitter.
  const Vec3 p0{30, -100, 0}, p1{30, 100, 0}, p2{30, 100, 60}, p3{30, -100, 60};
  scene.addMesh({Triangle{p0, p1, p2}, Triangle{p0, p2, p3}}, "concrete");

  Transmitter tx;
  tx.id = "tx";
  tx.position = {0, 0, 30};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);

  CoverageGrid g;
  g.origin = {40, -20, 0};  // behind the wall
  g.cellSize = 10.0;
  g.cols = 3;
  g.rows = 3;
  g.height = 1.5;

  const SimulationSettings s = sinrSettings();
  const CoverageResult cov = Simulator(s).runCoverage(scene, g);

  ASSERT_EQ(static_cast<int>(cov.sinrDb.size()), g.cellCount());
  int shadowed = 0;
  for (int i = 0; i < g.cellCount(); ++i)
    if (cov.powerDbm[i] == CoverageResult::NoSignal) {
      ++shadowed;
      EXPECT_EQ(cov.sinrDb[i], CoverageResult::NoSignal) << "cell " << i;
    }
  EXPECT_GT(shadowed, 0);  // the wall must shadow at least one cell
}

TEST(Sinr, CoverageArrayEmptyWhenDisabled) {
  Scene scene;
  scene.addTransmitter([] {
    Transmitter t;
    t.id = "tx";
    t.position = {0, 0, 20};
    t.frequencyHz = 3.5e9;
    t.powerDbm = 43.0;
    return t;
  }());
  CoverageGrid g;
  g.cols = 2;
  g.rows = 2;
  SimulationSettings s;
  s.maxReflections = 0;  // SINR disabled
  const CoverageResult cov = Simulator(s).runCoverage(scene, g);
  EXPECT_TRUE(cov.sinrDb.empty());
}
