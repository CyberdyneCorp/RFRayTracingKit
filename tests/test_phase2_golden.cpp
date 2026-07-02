#include <gtest/gtest.h>

#include <cmath>

#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {
Scene wallScene() {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  Transmitter tx;
  tx.id = "tx";
  tx.position = {100, 20, 20};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);
  Receiver rx;
  rx.id = "rx";
  rx.position = {200, 20, 10};
  scene.addReceiver(rx);
  return scene;
}
}  // namespace

// Golden: ray-launch aggregate power agrees with the image method on the
// single-wall scene within tolerance, given a sufficient ray budget.
// Pinned budget: 600k rays, 3 m capture radius -> <= 1 dB agreement (measured).
TEST(Phase2Golden, RayLaunchAgreesWithImageMethod) {
  const Scene scene = wallScene();

  SimulationSettings img;
  img.mode = PropagationMode::ImageMethod;
  img.maxReflections = 1;
  const double refPower = Simulator(img).run(scene).receiver("rx")->receivedPowerDbm;

  SimulationSettings ray;
  ray.mode = PropagationMode::RayLaunch;
  ray.maxReflections = 1;
  ray.raysPerTransmitter = 600000;
  ray.captureRadius = 3.0;
  ray.seed = 1;
  const double rayPower = Simulator(ray).run(scene).receiver("rx")->receivedPowerDbm;

  EXPECT_LE(std::abs(rayPower - refPower), 1.0)
      << "image=" << refPower << " ray=" << rayPower;
}

// Golden: a coverage grid over an unobstructed scene is fully covered and
// strongest nearest the transmitter's ground point.
TEST(Phase2Golden, CoverageGridPatternIsStable) {
  Scene scene;
  Transmitter tx;
  tx.id = "tx";
  tx.position = {50, 50, 30};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);

  CoverageGrid g;
  g.origin = {0, 0, 0};
  g.cellSize = 25.0;
  g.cols = 4;
  g.rows = 4;
  g.height = 1.5;

  SimulationSettings s;
  s.maxReflections = 0;
  const CoverageResult cov = Simulator(s).runCoverage(scene, g);

  for (double p : cov.powerDbm) ASSERT_TRUE(std::isfinite(p));

  // The strongest cell should be one of the central cells nearest the
  // transmitter's ground position (50,50); the four central cells are
  // equidistant, so assert proximity rather than a specific one.
  int best = 0;
  for (int i = 1; i < g.cellCount(); ++i)
    if (cov.powerDbm[i] > cov.powerDbm[best]) best = i;
  const Vec3 c = g.cellCenter(best / g.cols, best % g.cols);
  const double horiz = std::hypot(c.x() - 50.0, c.y() - 50.0);
  EXPECT_LE(horiz, g.cellSize);  // within one cell of the tx ground point
}
