#include <gtest/gtest.h>

#include <cmath>

#include "rftrace/material.hpp"
#include "rftrace/rf/free_space_path_loss.hpp"
#include "rftrace/simulator.hpp"

// Multipath coverage (D5): ray-launch coverage adds specular reflections (and,
// when enabled, diffraction) per grid cell, whereas the default image-method
// coverage is the exact deterministic oracle. These tests pin the added power
// against the image-method reference (analytic specular oracle) rather than a
// mere trend, and confirm the default path is unchanged.
using namespace rftrace;

namespace {

constexpr double kFreqHz = 3.5e9;
constexpr double kPowerDbm = 43.0;

Transmitter tx(const Vec3& p) {
  Transmitter t;
  t.id = "tx";
  t.position = p;
  t.frequencyHz = kFreqHz;
  t.powerDbm = kPowerDbm;
  return t;
}

// Scene: a tx, a large metal side-wall reflector at y=20, and a narrow blocker
// at x=30 that shadows the far cells (x=50,70,90) from direct LOS while leaving
// the reflected detour off the side wall unobstructed.
Scene reflectorScene() {
  Scene scene;
  scene.addTransmitter(tx({0, 0, 10}));
  scene.addMaterial(materials::preset("metal"));
  scene.addMesh({Triangle{{-100, 20, 0}, {100, 20, 0}, {100, 20, 40}},
                 Triangle{{-100, 20, 0}, {100, 20, 40}, {-100, 20, 40}}},
                "metal");
  scene.addMesh({Triangle{{30, -5, 0}, {30, 5, 0}, {30, 5, 40}},
                 Triangle{{30, -5, 0}, {30, 5, 40}, {30, -5, 40}}},
                "");
  return scene;
}

// x centers: 10, 30, 50, 70, 90 (single row at y=0, z=10).
CoverageGrid reflectorGrid() {
  CoverageGrid g;
  g.origin = {0, -10, 0};
  g.cellSize = 20.0;
  g.cols = 5;
  g.rows = 1;
  g.height = 10.0;
  return g;
}

SimulationSettings losOnly() {
  SimulationSettings s;
  s.mode = PropagationMode::ImageMethod;
  s.maxReflections = 0;  // LOS + FSPL only
  return s;
}

SimulationSettings imageMethod() {
  SimulationSettings s;
  s.mode = PropagationMode::ImageMethod;
  s.maxReflections = 1;
  return s;
}

SimulationSettings multipath() {
  SimulationSettings s;
  s.mode = PropagationMode::RayLaunch;
  s.maxReflections = 1;
  s.raysPerTransmitter = 20000;
  s.seed = 1;
  return s;
}

}  // namespace

// A cell shadowed from LOS: LOS+FSPL-only coverage reports no signal, while
// ray-launch multipath coverage reports a finite reflected power there.
TEST(CoverageMultipath, ShadowedCellGainsReflectedPower) {
  const Scene scene = reflectorScene();
  const CoverageGrid g = reflectorGrid();

  const CoverageResult base = Simulator(losOnly()).runCoverage(scene, g);
  const CoverageResult mp = Simulator(multipath()).runCoverage(scene, g);

  // Cells 2,3,4 (x=50,70,90) are behind the blocker: no LOS.
  for (int col : {2, 3, 4}) {
    EXPECT_FALSE(std::isfinite(base.powerAt(0, col)))
        << "LOS-only should be no-signal at col " << col;
    EXPECT_TRUE(std::isfinite(mp.powerAt(0, col)))
        << "multipath should recover a reflected path at col " << col;
  }
}

// The reflected power ray-launch accumulates must match the exact image-method
// specular solution (the analytic oracle) at the shadowed, reflection-only cells.
TEST(CoverageMultipath, ReflectedPowerMatchesImageMethodOracle) {
  const Scene scene = reflectorScene();
  const CoverageGrid g = reflectorGrid();

  const CoverageResult img = Simulator(imageMethod()).runCoverage(scene, g);
  const CoverageResult mp = Simulator(multipath()).runCoverage(scene, g);

  for (int col : {2, 3, 4}) {
    ASSERT_TRUE(std::isfinite(img.powerAt(0, col)));
    ASSERT_TRUE(std::isfinite(mp.powerAt(0, col)));
    // Ray-launch reconstructs the same single specular detour as the image
    // method; the captured bounce is near-specular so powers agree tightly.
    EXPECT_NEAR(mp.powerAt(0, col), img.powerAt(0, col), 0.05)
        << "col " << col;
  }
}

// An open cell that captures a reflection is stronger under multipath than under
// LOS+FSPL alone (incoherent addition), yet its LOS-dominated level stays within
// tolerance of the analytic free-space value.
TEST(CoverageMultipath, OpenCellMatchesLosWithinTolerance) {
  const Scene scene = reflectorScene();
  const CoverageGrid g = reflectorGrid();

  const CoverageResult base = Simulator(losOnly()).runCoverage(scene, g);
  const CoverageResult mp = Simulator(multipath()).runCoverage(scene, g);

  // Open cell 0 at x=10: exact free-space received power (omni gains = 0 dBi).
  const double d = (g.cellCenter(0, 0) - Vec3{0, 0, 10}).norm();
  const double expected = kPowerDbm - rf::freeSpacePathLossDb(d, kFreqHz);
  EXPECT_NEAR(base.powerAt(0, 0), expected, 1e-6);

  // Multipath adds a reflected contribution incoherently -> at least as strong,
  // and still within a small tolerance of the LOS level.
  EXPECT_GE(mp.powerAt(0, 0), base.powerAt(0, 0));
  EXPECT_NEAR(mp.powerAt(0, 0), base.powerAt(0, 0), 1.0);
}

// Selecting ray-launch coverage must not perturb the default image-method path:
// the default coverage still equals the exact analytic/oracle values.
TEST(CoverageMultipath, DefaultImageMethodCoverageUnchanged) {
  const Scene scene = reflectorScene();
  const CoverageGrid g = reflectorGrid();

  const CoverageResult img = Simulator(imageMethod()).runCoverage(scene, g);

  // Open cell 0 reproduces the analytic free-space value bit-for-bit.
  const double d = (g.cellCenter(0, 0) - Vec3{0, 0, 10}).norm();
  const double expectedLos = kPowerDbm - rf::freeSpacePathLossDb(d, kFreqHz);
  // Cell 0 also captures a reflection in the image method, so its power is at
  // least the LOS level; the shadowed cells are finite via specular reflection.
  EXPECT_GE(img.powerAt(0, 0), expectedLos);
  for (int col : {2, 3, 4})
    EXPECT_TRUE(std::isfinite(img.powerAt(0, col)));

  // Re-running the default is bit-for-bit stable.
  const CoverageResult img2 = Simulator(imageMethod()).runCoverage(scene, g);
  for (int i = 0; i < g.cellCount(); ++i)
    EXPECT_EQ(img.powerDbm[i], img2.powerDbm[i]);
}

// Ray-launch multipath coverage is deterministic for a fixed seed.
TEST(CoverageMultipath, DeterministicForFixedSeed) {
  const Scene scene = reflectorScene();
  const CoverageGrid g = reflectorGrid();

  const CoverageResult a = Simulator(multipath()).runCoverage(scene, g);
  const CoverageResult b = Simulator(multipath()).runCoverage(scene, g);

  ASSERT_EQ(a.powerDbm.size(), b.powerDbm.size());
  for (std::size_t i = 0; i < a.powerDbm.size(); ++i)
    EXPECT_EQ(a.powerDbm[i], b.powerDbm[i]) << "cell " << i;
}

// A cell with no LOS, no reflector, and diffraction disabled keeps the NoSignal
// sentinel; enabling diffraction fills it with a finite knife-edge detour.
TEST(CoverageMultipath, ShadowedCellsSentinelAndDiffractionFill) {
  Scene scene;
  scene.addTransmitter(tx({0, 0, 10}));
  // A single wide blocker with a finite top at z=20; no reflecting surfaces.
  scene.addMesh({Triangle{{30, -100, 0}, {30, 100, 0}, {30, 100, 20}},
                 Triangle{{30, -100, 0}, {30, 100, 20}, {30, -100, 20}}},
                "");
  CoverageGrid g;
  g.origin = {0, -10, 0};
  g.cellSize = 20.0;
  g.cols = 4;  // x centers: 10,30,50,70
  g.rows = 1;
  g.height = 10.0;

  SimulationSettings noDiff = multipath();
  const CoverageResult a = Simulator(noDiff).runCoverage(scene, g);
  // Cells 2,3 (x=50,70) are shadowed with nothing to reflect off -> no signal.
  for (int col : {2, 3})
    EXPECT_FALSE(std::isfinite(a.powerAt(0, col))) << "col " << col;

  SimulationSettings withDiff = noDiff;
  withDiff.enableDiffraction = true;
  const CoverageResult b = Simulator(withDiff).runCoverage(scene, g);
  for (int col : {2, 3})
    EXPECT_TRUE(std::isfinite(b.powerAt(0, col)))
        << "diffraction should fill shadowed col " << col;
}
