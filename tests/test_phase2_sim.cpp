#include <gtest/gtest.h>

#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {
Transmitter tx(const Vec3& p, const std::string& id = "tx") {
  Transmitter t;
  t.id = id;
  t.position = p;
  t.frequencyHz = 3.5e9;
  t.powerDbm = 43.0;
  return t;
}
Receiver rx(const Vec3& p, const std::string& id = "rx") {
  Receiver r;
  r.id = id;
  r.position = p;
  return r;
}

// Two perpendicular walls forming a corner (planes x=0 and y=0).
void addCorner(Scene& scene) {
  scene.addMesh({Triangle{{0, 0, 0}, {0, 50, 0}, {0, 50, 30}},
                 Triangle{{0, 0, 0}, {0, 50, 30}, {0, 0, 30}}},
                "");  // wall A: x=0
  scene.addMesh({Triangle{{0, 0, 0}, {50, 0, 0}, {50, 0, 30}},
                 Triangle{{0, 0, 0}, {50, 0, 30}, {0, 0, 30}}},
                "");  // wall B: y=0
}
}  // namespace

TEST(Phase2Sim, ImageMethodModeProducesLos) {
  Scene scene;
  scene.addTransmitter(tx({0, 0, 10}));
  scene.addReceiver(rx({100, 0, 10}));
  SimulationSettings s;
  s.mode = PropagationMode::ImageMethod;
  const RFResult r = Simulator(s).run(scene);
  ASSERT_TRUE(r.receiver("rx")->hasSignal);
}

TEST(Phase2Sim, RayLaunchModeProducesLos) {
  Scene scene;
  scene.addTransmitter(tx({0, 0, 10}));
  scene.addReceiver(rx({100, 0, 10}));
  SimulationSettings s;
  s.mode = PropagationMode::RayLaunch;
  s.raysPerTransmitter = 1000;
  const RFResult r = Simulator(s).run(scene);
  // LOS is deterministic in both modes.
  ASSERT_TRUE(r.receiver("rx")->hasSignal);
}

TEST(Phase2Sim, TwoBounceReflectionFound) {
  Scene scene;
  addCorner(scene);
  scene.addTransmitter(tx({10, 20, 10}));
  scene.addReceiver(rx({20, 10, 10}));

  SimulationSettings s;
  s.mode = PropagationMode::ImageMethod;
  s.maxReflections = 2;
  const RFResult r = Simulator(s).run(scene);

  bool twoBounce = false;
  for (const RFPath& p : r.receiver("rx")->paths) {
    if (p.reflections == 2) {
      twoBounce = true;
      EXPECT_EQ(p.points.size(), 4u);  // tx + 2 bounces + rx
    }
  }
  EXPECT_TRUE(twoBounce);
}

TEST(Phase2Sim, DepthBoundedInBothModes) {
  for (PropagationMode mode :
       {PropagationMode::ImageMethod, PropagationMode::RayLaunch}) {
    Scene scene;
    addCorner(scene);
    scene.addTransmitter(tx({10, 20, 10}));
    scene.addReceiver(rx({20, 10, 10}));
    SimulationSettings s;
    s.mode = mode;
    s.maxReflections = 1;
    s.raysPerTransmitter = 5000;
    const RFResult r = Simulator(s).run(scene);
    for (const RFPath& p : r.receiver("rx")->paths)
      EXPECT_LE(p.reflections, 1);
  }
}
