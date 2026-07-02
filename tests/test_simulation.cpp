#include <gtest/gtest.h>

#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {
// A wall in the plane y = 100 spanning x∈[0,300], z∈[0,50].
void addWall(Scene& scene, const std::string& material = "") {
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, material);
}

Transmitter makeTx(const Vec3& p) {
  Transmitter tx;
  tx.id = "tx";
  tx.position = p;
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  return tx;
}
Receiver makeRx(const Vec3& p) {
  Receiver rx;
  rx.id = "rx";
  rx.position = p;
  return rx;
}
}  // namespace

TEST(Simulation, UnobstructedLosProducesDirectPath) {
  Scene scene;
  scene.addTransmitter(makeTx({0, 0, 10}));
  scene.addReceiver(makeRx({100, 0, 10}));

  SimulationSettings s;
  s.maxReflections = 0;
  const RFResult r = Simulator(s).run(scene);

  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_TRUE(rr->hasSignal);
  ASSERT_EQ(rr->paths.size(), 1u);
  EXPECT_EQ(rr->paths[0].type, PathType::LOS);
  EXPECT_EQ(rr->paths[0].points.size(), 2u);
}

TEST(Simulation, ObstructedLosProducesNoDirectPath) {
  Scene scene;
  addWall(scene);  // wall at y=100 between tx and rx
  scene.addTransmitter(makeTx({150, 50, 10}));
  scene.addReceiver(makeRx({150, 150, 10}));

  SimulationSettings s;
  s.maxReflections = 0;
  const RFResult r = Simulator(s).run(scene);

  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  EXPECT_FALSE(rr->hasSignal);  // blocked, no LOS, no reflections requested
}

TEST(Simulation, SingleWallReflectionIsFound) {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  addWall(scene, "concrete");
  scene.addTransmitter(makeTx({100, 20, 20}));
  scene.addReceiver(makeRx({200, 20, 10}));

  SimulationSettings s;
  s.maxReflections = 1;
  const RFResult r = Simulator(s).run(scene);

  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  int reflections = 0;
  for (const RFPath& p : rr->paths) {
    if (p.type == PathType::Reflection) {
      ++reflections;
      ASSERT_EQ(p.points.size(), 3u);
      EXPECT_NEAR(p.points[1].y(), 100.0, 1e-6);  // bounce on the wall
      ASSERT_EQ(p.materialHits.size(), 1u);
      EXPECT_EQ(p.materialHits[0], "concrete");
    }
  }
  EXPECT_EQ(reflections, 1);
}

TEST(Simulation, InvalidImagePointIsRejected) {
  // Wall too small: the geometric reflection point falls outside the triangle.
  Scene scene;
  scene.addMesh({Triangle{{0, 100, 0}, {1, 100, 0}, {0, 100, 1}}}, "");
  scene.addTransmitter(makeTx({100, 20, 20}));
  scene.addReceiver(makeRx({200, 20, 10}));

  SimulationSettings s;
  s.maxReflections = 1;
  const RFResult r = Simulator(s).run(scene);

  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  for (const RFPath& p : rr->paths)
    EXPECT_NE(p.type, PathType::Reflection);
}

TEST(Simulation, BounceCountBounded) {
  Scene scene;
  addWall(scene);
  scene.addTransmitter(makeTx({100, 20, 20}));
  scene.addReceiver(makeRx({200, 20, 10}));

  SimulationSettings s;
  s.maxReflections = 1;
  const RFResult r = Simulator(s).run(scene);
  for (const RFPath& p : r.receiver("rx")->paths)
    EXPECT_LE(p.reflections, 1);
}

TEST(Simulation, DefaultSettingsAreRunnable) {
  SimulationSettings s;
  EXPECT_EQ(s.backend, Backend::CPU);
  EXPECT_GE(s.maxReflections, 0);
  EXPECT_GT(s.captureRadius, 0.0);
}

TEST(Simulation, RepeatedRunsAreDeterministic) {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  addWall(scene, "concrete");
  scene.addTransmitter(makeTx({100, 20, 20}));
  scene.addReceiver(makeRx({200, 20, 10}));

  SimulationSettings s;
  s.maxReflections = 1;
  const RFResult a = Simulator(s).run(scene);
  const RFResult b = Simulator(s).run(scene);

  ASSERT_EQ(a.receivers.size(), b.receivers.size());
  const ReceiverResult& ra = a.receivers[0];
  const ReceiverResult& rb = b.receivers[0];
  EXPECT_EQ(ra.paths.size(), rb.paths.size());
  EXPECT_DOUBLE_EQ(ra.receivedPowerDbm, rb.receivedPowerDbm);
}

TEST(Simulation, UnavailableBackendFallsBackToCpu) {
  Scene scene;
  scene.addTransmitter(makeTx({0, 0, 10}));
  scene.addReceiver(makeRx({100, 0, 10}));
  SimulationSettings s;
  s.backend = Backend::CUDA;  // not compiled in Phase 1
  s.allowBackendFallback = true;
  EXPECT_NO_THROW(Simulator(s).run(scene));
}
