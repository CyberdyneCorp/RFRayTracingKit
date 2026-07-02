#include <gtest/gtest.h>

#include "rftrace/rf/free_space_path_loss.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {
Transmitter tx(const Vec3& p) {
  Transmitter t;
  t.id = "tx";
  t.position = p;
  t.frequencyHz = 3.5e9;
  t.powerDbm = 43.0;
  return t;
}
Receiver rx(const Vec3& p) {
  Receiver r;
  r.id = "rx";
  r.position = p;
  return r;
}
}  // namespace

// Golden: empty scene (no geometry) -> pure LOS with a known link budget.
TEST(Golden, EmptySceneLos) {
  Scene scene;
  scene.addTransmitter(tx({0, 0, 30}));
  scene.addReceiver(rx({100, 0, 1.5}));

  SimulationSettings s;
  s.maxReflections = 2;  // no geometry, so still only LOS
  const RFResult r = Simulator(s).run(scene);

  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_TRUE(rr->hasSignal);
  ASSERT_EQ(rr->paths.size(), 1u);

  const double dist = (Vec3{100, 0, 1.5} - Vec3{0, 0, 30}).norm();
  const double expected = 43.0 - rf::freeSpacePathLossDb(dist, 3.5e9);
  EXPECT_NEAR(rr->paths[0].receivedPowerDbm, expected, 1e-9);
}

// Golden: single wall -> LOS plus one reflection; reflection is weaker.
TEST(Golden, SingleWallReflection) {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  scene.addTransmitter(tx({100, 20, 20}));
  scene.addReceiver(rx({200, 20, 10}));

  SimulationSettings s;
  s.maxReflections = 1;
  const RFResult r = Simulator(s).run(scene);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_EQ(rr->paths.size(), 2u);  // LOS + reflection

  double los = 0, refl = 0;
  for (const RFPath& p : rr->paths) {
    if (p.type == PathType::LOS) los = p.receivedPowerDbm;
    else refl = p.receivedPowerDbm;
  }
  EXPECT_LT(refl, los);                       // longer + lossy path is weaker
  EXPECT_GT(rr->receivedPowerDbm, los - 0.1);  // aggregate ≥ LOS alone
}

// Golden: two parallel walls form a canyon -> LOS plus a reflection off each.
TEST(Golden, TwoBuildingCanyon) {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  // Wall A at y=100, wall B at y=-100.
  scene.addMesh({Triangle{{0, 100, 0}, {300, 100, 0}, {300, 100, 50}},
                 Triangle{{0, 100, 0}, {300, 100, 50}, {0, 100, 50}}},
                "concrete");
  scene.addMesh({Triangle{{0, -100, 0}, {300, -100, 0}, {300, -100, 50}},
                 Triangle{{0, -100, 0}, {300, -100, 50}, {0, -100, 50}}},
                "concrete");
  scene.addTransmitter(tx({100, 0, 20}));
  scene.addReceiver(rx({200, 0, 10}));

  SimulationSettings s;
  s.maxReflections = 1;
  const RFResult r = Simulator(s).run(scene);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);

  int los = 0, refl = 0;
  for (const RFPath& p : rr->paths) {
    if (p.type == PathType::LOS) ++los;
    else if (p.type == PathType::Reflection) ++refl;
  }
  EXPECT_EQ(los, 1);
  EXPECT_GE(refl, 2);  // one bounce off each wall
}
