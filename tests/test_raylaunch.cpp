#include <gtest/gtest.h>

#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {
// Phase 1 single-wall scene (wall at y=100).
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

SimulationSettings rayLaunchSettings() {
  SimulationSettings s;
  s.mode = PropagationMode::RayLaunch;
  s.maxReflections = 1;
  s.raysPerTransmitter = 400000;
  s.captureRadius = 5.0;
  s.seed = 7;
  return s;
}
}  // namespace

TEST(RayLaunch, CapturesReflectionPath) {
  const RFResult r = Simulator(rayLaunchSettings()).run(wallScene());
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  int reflections = 0;
  for (const RFPath& p : rr->paths)
    if (p.type == PathType::Reflection) ++reflections;
  EXPECT_GE(reflections, 1);  // a launched ray reflected into the capture sphere
}

TEST(RayLaunch, DeduplicatesToFewPaths) {
  // Many rays hit the same 2-triangle wall; dedup collapses them per signature.
  const RFResult r = Simulator(rayLaunchSettings()).run(wallScene());
  int reflections = 0;
  for (const RFPath& p : r.receiver("rx")->paths)
    if (p.type == PathType::Reflection) ++reflections;
  EXPECT_LE(reflections, 4);  // not proportional to the 400k ray budget
}

TEST(RayLaunch, SameSeedIsReproducible) {
  const RFResult a = Simulator(rayLaunchSettings()).run(wallScene());
  const RFResult b = Simulator(rayLaunchSettings()).run(wallScene());
  ASSERT_EQ(a.receivers.size(), b.receivers.size());
  EXPECT_EQ(a.receivers[0].paths.size(), b.receivers[0].paths.size());
  EXPECT_DOUBLE_EQ(a.receivers[0].receivedPowerDbm,
                   b.receivers[0].receivedPowerDbm);
}

TEST(RayLaunch, RayOutsideCaptureRadiusIgnored) {
  // Tiny capture radius + few rays: the exact specular ray is unlikely to be
  // captured, so no reflection path should appear (LOS still does).
  Scene scene = wallScene();
  SimulationSettings s = rayLaunchSettings();
  s.raysPerTransmitter = 200;
  s.captureRadius = 0.001;
  const RFResult r = Simulator(s).run(scene);
  for (const RFPath& p : r.receiver("rx")->paths)
    EXPECT_NE(p.type, PathType::Reflection);
}
