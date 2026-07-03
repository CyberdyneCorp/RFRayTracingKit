#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "rftrace/simulator.hpp"

// Phase 2 (cpu-full-run-acceleration): the deterministic parallel-for over the
// independent per-receiver (run) and per-cell (coverage image-method) loops must
// produce BIT-FOR-BIT identical results for any threadCount and across repeated
// runs. threadCount==1 is the exact pre-change serial path; the golden suites
// pin the absolute numeric values, so here we only assert cross-thread-count
// equality and repeat-run determinism.

using namespace rftrace;

namespace {

// NaN-safe exact equality: compares the raw IEEE-754 bit pattern so NoSignal /
// SINR NaN sentinels must match bit-for-bit (== is always false for NaN).
bool bitsEqual(double a, double b) {
  std::uint64_t ua, ub;
  std::memcpy(&ua, &a, sizeof ua);
  std::memcpy(&ub, &b, sizeof ub);
  return ua == ub;
}

bool bitsEqual(const Vec3& a, const Vec3& b) {
  return bitsEqual(a.x(), b.x()) && bitsEqual(a.y(), b.y()) &&
         bitsEqual(a.z(), b.z());
}

bool bitsEqual(const std::vector<double>& a, const std::vector<double>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (!bitsEqual(a[i], b[i])) return false;
  return true;
}

void expectPathEqual(const RFPath& a, const RFPath& b) {
  EXPECT_EQ(a.transmitterId, b.transmitterId);
  EXPECT_EQ(a.receiverId, b.receiverId);
  EXPECT_EQ(a.type, b.type);
  EXPECT_EQ(a.reflections, b.reflections);
  EXPECT_EQ(a.diffractions, b.diffractions);
  EXPECT_EQ(a.materialHits, b.materialHits);
  EXPECT_TRUE(bitsEqual(a.receivedPowerDbm, b.receivedPowerDbm));
  EXPECT_TRUE(bitsEqual(a.pathLossDb, b.pathLossDb));
  EXPECT_TRUE(bitsEqual(a.phaseRad, b.phaseRad));
  EXPECT_TRUE(bitsEqual(a.delaySeconds, b.delaySeconds));
  EXPECT_TRUE(bitsEqual(a.dopplerHz, b.dopplerHz));
  ASSERT_EQ(a.points.size(), b.points.size());
  for (std::size_t i = 0; i < a.points.size(); ++i)
    EXPECT_TRUE(bitsEqual(a.points[i], b.points[i]));
}

void expectReceiverEqual(const ReceiverResult& a, const ReceiverResult& b) {
  EXPECT_EQ(a.receiverId, b.receiverId);
  EXPECT_EQ(a.hasSignal, b.hasSignal);
  EXPECT_TRUE(bitsEqual(a.receivedPowerDbm, b.receivedPowerDbm));
  EXPECT_TRUE(bitsEqual(a.pathLossDb, b.pathLossDb));
  EXPECT_TRUE(bitsEqual(a.delaySpreadNs, b.delaySpreadNs));
  EXPECT_TRUE(bitsEqual(a.phaseRad, b.phaseRad));
  EXPECT_TRUE(bitsEqual(a.sinrDb, b.sinrDb));
  EXPECT_TRUE(bitsEqual(a.interferencePowerDbm, b.interferencePowerDbm));
  EXPECT_EQ(a.servingTransmitterId, b.servingTransmitterId);
  ASSERT_EQ(a.paths.size(), b.paths.size());
  for (std::size_t i = 0; i < a.paths.size(); ++i)
    expectPathEqual(a.paths[i], b.paths[i]);
}

void expectResultEqual(const RFResult& a, const RFResult& b) {
  ASSERT_EQ(a.receivers.size(), b.receivers.size());
  for (std::size_t i = 0; i < a.receivers.size(); ++i)
    expectReceiverEqual(a.receivers[i], b.receivers[i]);
}

void expectCoverageEqual(const CoverageResult& a, const CoverageResult& b) {
  EXPECT_TRUE(bitsEqual(a.powerDbm, b.powerDbm));
  EXPECT_TRUE(bitsEqual(a.pathLossDb, b.pathLossDb));
  EXPECT_TRUE(bitsEqual(a.sinrDb, b.sinrDb));
}

// Single-wall image-method scene (as in test_phase2_golden) with several
// receivers so the per-receiver reflection loop has real parallel work.
Scene multiReceiverScene() {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");

  Transmitter tx0;
  tx0.id = "tx0";
  tx0.position = {100, 20, 20};
  tx0.frequencyHz = 3.5e9;
  tx0.powerDbm = 43.0;
  scene.addTransmitter(tx0);
  Transmitter tx1;
  tx1.id = "tx1";
  tx1.position = {220, 30, 25};
  tx1.frequencyHz = 3.5e9;
  tx1.powerDbm = 40.0;
  scene.addTransmitter(tx1);

  for (int i = 0; i < 24; ++i) {
    Receiver rx;
    rx.id = "rx" + std::to_string(i);
    rx.position = {20.0 + 10.0 * i, 20.0 + (i % 3) * 5.0, 10.0};
    scene.addReceiver(rx);
  }
  return scene;
}

int hardware() {
  const unsigned hc = std::thread::hardware_concurrency();
  return hc == 0 ? 4 : static_cast<int>(hc);
}

}  // namespace

// run(): image-method per-receiver reflection loop is thread-count invariant.
TEST(Phase2Threading, RunImageMethodThreadCountInvariant) {
  const Scene scene = multiReceiverScene();

  SimulationSettings base;
  base.mode = PropagationMode::ImageMethod;
  base.maxReflections = 1;
  base.enableSinr = true;

  SimulationSettings serial = base;
  serial.threadCount = 1;
  SimulationSettings autoT = base;
  autoT.threadCount = 0;  // => hardware concurrency
  SimulationSettings explicitT = base;
  explicitT.threadCount = hardware();

  const RFResult r1 = Simulator(serial).run(scene);
  const RFResult r0 = Simulator(autoT).run(scene);
  const RFResult rn = Simulator(explicitT).run(scene);

  ASSERT_FALSE(r1.receivers.empty());
  expectResultEqual(r1, r0);
  expectResultEqual(r1, rn);
}

// runCoverage() image-method per-cell loop is thread-count invariant, including
// the SINR array + NoSignal/NaN sentinels.
TEST(Phase2Threading, CoverageImageMethodThreadCountInvariant) {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  Transmitter tx;
  tx.id = "tx";
  tx.position = {150, 20, 30};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);

  CoverageGrid g;
  g.origin = {0, 0, 0};
  g.cellSize = 5.0;
  g.cols = 40;
  g.rows = 30;
  g.height = 1.5;

  SimulationSettings base;
  base.mode = PropagationMode::ImageMethod;
  base.maxReflections = 1;
  base.enableSinr = true;

  SimulationSettings serial = base;
  serial.threadCount = 1;
  SimulationSettings autoT = base;
  autoT.threadCount = 0;
  SimulationSettings explicitT = base;
  explicitT.threadCount = hardware();

  const CoverageResult c1 = Simulator(serial).runCoverage(scene, g);
  const CoverageResult c0 = Simulator(autoT).runCoverage(scene, g);
  const CoverageResult cn = Simulator(explicitT).runCoverage(scene, g);

  ASSERT_EQ(static_cast<int>(c1.powerDbm.size()), g.cellCount());
  expectCoverageEqual(c1, c0);
  expectCoverageEqual(c1, cn);
}

// Repeat-run determinism: with threadCount = hardware, N runs are byte-identical
// (guards against scheduling-dependent nondeterminism).
TEST(Phase2Threading, RepeatRunDeterministic) {
  const Scene scene = multiReceiverScene();
  SimulationSettings s;
  s.mode = PropagationMode::ImageMethod;
  s.maxReflections = 1;
  s.enableSinr = true;
  s.threadCount = hardware();

  const RFResult ref = Simulator(s).run(scene);
  for (int rep = 0; rep < 8; ++rep) {
    const RFResult r = Simulator(s).run(scene);
    expectResultEqual(ref, r);
  }
}

TEST(Phase2Threading, RepeatCoverageDeterministic) {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  Transmitter tx;
  tx.id = "tx";
  tx.position = {150, 20, 30};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);

  CoverageGrid g;
  g.origin = {0, 0, 0};
  g.cellSize = 5.0;
  g.cols = 40;
  g.rows = 30;
  g.height = 1.5;

  SimulationSettings s;
  s.mode = PropagationMode::ImageMethod;
  s.maxReflections = 1;
  s.enableSinr = true;
  s.threadCount = hardware();

  const CoverageResult ref = Simulator(s).runCoverage(scene, g);
  for (int rep = 0; rep < 8; ++rep) {
    const CoverageResult cov = Simulator(s).runCoverage(scene, g);
    expectCoverageEqual(ref, cov);
  }
}
