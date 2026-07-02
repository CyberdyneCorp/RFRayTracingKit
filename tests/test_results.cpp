#include <gtest/gtest.h>

#include <algorithm>

#include "rftrace/exporters/csv_exporter.hpp"
#include "rftrace/exporters/json_exporter.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {
RFResult runReflectionScene() {
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

  SimulationSettings s;
  s.maxReflections = 1;
  s.simulationId = "results_test";
  return Simulator(s).run(scene);
}
}  // namespace

TEST(Results, LosPathRecordShape) {
  Scene scene;
  Transmitter tx;
  tx.id = "tx";
  tx.position = {0, 0, 10};
  tx.frequencyHz = 3.5e9;
  scene.addTransmitter(tx);
  Receiver rx;
  rx.id = "rx";
  rx.position = {100, 0, 10};
  scene.addReceiver(rx);

  SimulationSettings s;
  s.maxReflections = 0;
  const RFResult r = Simulator(s).run(scene);
  const RFPath& p = r.receiver("rx")->paths.at(0);
  EXPECT_EQ(p.type, PathType::LOS);
  EXPECT_EQ(p.points.size(), 2u);
  EXPECT_EQ(p.reflections, 0);
  EXPECT_TRUE(p.materialHits.empty());
  EXPECT_GT(p.pathLossDb, 0.0);
}

TEST(Results, ReflectionPathRecordsBounceAndMaterial) {
  const RFResult r = runReflectionScene();
  bool found = false;
  for (const RFPath& p : r.receiver("rx")->paths) {
    if (p.type == PathType::Reflection) {
      found = true;
      EXPECT_EQ(p.points.size(), 3u);
      EXPECT_EQ(p.reflections, 1);
      ASSERT_EQ(p.materialHits.size(), 1u);
      EXPECT_EQ(p.materialHits[0], "concrete");
    }
  }
  EXPECT_TRUE(found);
}

TEST(Results, DelaySpreadFromMultipath) {
  const RFResult r = runReflectionScene();
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_GE(rr->paths.size(), 2u);  // LOS + reflection
  EXPECT_GT(rr->delaySpreadNs, 0.0);
}

TEST(Results, JsonRoundTrip) {
  const RFResult r = runReflectionScene();
  const std::string text = io::resultToJsonString(r);
  const RFResult back = io::resultFromJsonString(text);

  ASSERT_EQ(back.receivers.size(), r.receivers.size());
  EXPECT_EQ(back.simulationId, r.simulationId);
  const ReceiverResult& a = r.receivers[0];
  const ReceiverResult& b = back.receivers[0];
  EXPECT_EQ(a.paths.size(), b.paths.size());
  EXPECT_NEAR(a.receivedPowerDbm, b.receivedPowerDbm, 1e-6);
  EXPECT_NEAR(a.pathLossDb, b.pathLossDb, 1e-6);
}

TEST(Results, CsvHasHeaderAndRowPerReceiver) {
  const RFResult r = runReflectionScene();
  const std::string csv = io::receiversToCsvString(r);
  EXPECT_NE(csv.find("receiver_id"), std::string::npos);
  // Header + one data row.
  const auto lines = std::count(csv.begin(), csv.end(), '\n');
  EXPECT_EQ(lines, 2);
}

TEST(Results, CsvNoSignalSentinel) {
  // Fully blocked receiver -> no signal -> empty power fields.
  Scene scene;
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "");
  Transmitter tx;
  tx.id = "tx";
  tx.position = {150, 50, 10};
  scene.addTransmitter(tx);
  Receiver rx;
  rx.id = "rx";
  rx.position = {150, 150, 10};
  scene.addReceiver(rx);

  SimulationSettings s;
  s.maxReflections = 0;
  const RFResult r = Simulator(s).run(scene);
  ASSERT_FALSE(r.receiver("rx")->hasSignal);

  const std::string csv = io::receiversToCsvString(r);
  EXPECT_NE(csv.find(",,,"), std::string::npos);  // empty power/loss/spread
}
