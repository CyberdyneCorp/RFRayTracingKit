#include <gtest/gtest.h>

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rftrace/exporters/csv_exporter.hpp"
#include "rftrace/exporters/json_exporter.hpp"
#include "rftrace/route.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {

// Open-field scene: one transmitter, no geometry, so every route sample has
// deterministic LOS.
Scene openFieldScene() {
  Scene scene;
  Transmitter tx;
  tx.id = "tx0";
  tx.position = {0, 0, 10};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);
  return scene;
}

Route straightRoute(double spacing) {
  Route r;
  r.id = "drive";
  // 100 m straight line at receiver height.
  r.waypoints = {Vec3{0, 0, 1.5}, Vec3{100, 0, 1.5}};
  r.sampleSpacing = spacing;
  return r;
}

// Count data rows (all lines after the header) in a CSV string.
std::size_t csvDataRows(const std::string& csv) {
  std::istringstream is(csv);
  std::string line;
  std::size_t rows = 0;
  bool header = true;
  while (std::getline(is, line)) {
    if (line.empty()) continue;
    if (header) {
      header = false;
      continue;
    }
    ++rows;
  }
  return rows;
}

}  // namespace

// --- Route sampling ----------------------------------------------------------

TEST(RouteSampling, SpacedAlongPolyline) {
  Route r = straightRoute(10.0);  // 100 m / 10 m => 11 samples (0..100)
  const auto pts = r.sample();

  ASSERT_EQ(pts.size(), 11u);
  EXPECT_DOUBLE_EQ(pts.front().distanceMeters, 0.0);
  EXPECT_DOUBLE_EQ(pts.back().distanceMeters, 100.0);
  // First and last samples land on the waypoints.
  EXPECT_DOUBLE_EQ(pts.front().position.x(), 0.0);
  EXPECT_DOUBLE_EQ(pts.back().position.x(), 100.0);
  // Interior spacing is ~s along the polyline.
  for (std::size_t i = 1; i < pts.size(); ++i) {
    const double step = pts[i].distanceMeters - pts[i - 1].distanceMeters;
    EXPECT_NEAR(step, 10.0, 1e-9);
    EXPECT_NEAR(pts[i].position.x(), pts[i - 1].position.x() + 10.0, 1e-9);
  }
}

TEST(RouteSampling, MultiSegmentTracksBend) {
  Route r;
  r.waypoints = {Vec3{0, 0, 0}, Vec3{10, 0, 0}, Vec3{10, 10, 0}};  // L-shape, 20 m
  r.sampleSpacing = 5.0;
  const auto pts = r.sample();

  ASSERT_EQ(pts.size(), 5u);  // 0,5,10,15,20
  EXPECT_NEAR(pts[2].position.x(), 10.0, 1e-9);  // corner at d=10
  EXPECT_NEAR(pts[2].position.y(), 0.0, 1e-9);
  EXPECT_NEAR(pts[3].position.x(), 10.0, 1e-9);  // up the second leg
  EXPECT_NEAR(pts[3].position.y(), 5.0, 1e-9);
  EXPECT_NEAR(pts.back().position.y(), 10.0, 1e-9);
}

TEST(RouteSampling, DegenerateRoutes) {
  Route empty;
  EXPECT_TRUE(empty.sample().empty());

  Route single;
  single.waypoints = {Vec3{1, 2, 3}};
  ASSERT_EQ(single.sample().size(), 1u);
  EXPECT_DOUBLE_EQ(single.sample().front().distanceMeters, 0.0);
}

// --- Route simulation --------------------------------------------------------

TEST(RouteSimulation, KSamplesGiveKOrderedResults) {
  Scene scene = openFieldScene();
  SimulationSettings s;
  s.maxReflections = 0;  // LOS only
  Simulator sim(s);

  Route r = straightRoute(10.0);
  const auto expected = r.sample();
  const RouteResult res = sim.runRoute(scene, r);

  ASSERT_EQ(res.samples.size(), expected.size());
  EXPECT_EQ(res.routeId, "drive");
  for (std::size_t i = 0; i < res.samples.size(); ++i) {
    EXPECT_EQ(res.samples[i].index, static_cast<int>(i));
    EXPECT_DOUBLE_EQ(res.samples[i].distanceMeters, expected[i].distanceMeters);
    EXPECT_DOUBLE_EQ(res.samples[i].position.x(), expected[i].position.x());
    EXPECT_TRUE(res.samples[i].hasSignal);
  }
}

TEST(RouteSimulation, PowerFallsWithDistanceFromTx) {
  Scene scene = openFieldScene();  // tx at x=0
  SimulationSettings s;
  s.maxReflections = 0;
  Simulator sim(s);

  const RouteResult res = sim.runRoute(scene, straightRoute(10.0));
  // Route recedes from the transmitter, so received power is non-increasing.
  for (std::size_t i = 1; i < res.samples.size(); ++i)
    EXPECT_LE(res.samples[i].receivedPowerDbm,
              res.samples[i - 1].receivedPowerDbm + 1e-9);
}

// --- Export ------------------------------------------------------------------

TEST(RouteExport, CsvHasOneRowPerSampleInOrder) {
  Scene scene = openFieldScene();
  SimulationSettings s;
  s.maxReflections = 0;
  Simulator sim(s);

  Route r = straightRoute(10.0);
  const RouteResult res = sim.runRoute(scene, r);
  const std::string csv = io::routeToCsvString(res);

  EXPECT_EQ(csvDataRows(csv), res.samples.size());
  EXPECT_NE(csv.find("index,distance_m,x,y,z"), std::string::npos);
  // Default (no SINR) => no SINR columns.
  EXPECT_EQ(csv.find("sinr_db"), std::string::npos);

  // Data rows appear in route order (index column is 0,1,2,...).
  std::istringstream is(csv);
  std::string line;
  std::getline(is, line);  // header
  int expectedIndex = 0;
  while (std::getline(is, line)) {
    if (line.empty()) continue;
    const int idx = std::stoi(line.substr(0, line.find(',')));
    EXPECT_EQ(idx, expectedIndex++);
  }
}

TEST(RouteExport, JsonSamplesInOrder) {
  Scene scene = openFieldScene();
  SimulationSettings s;
  s.maxReflections = 0;
  Simulator sim(s);

  const RouteResult res = sim.runRoute(scene, straightRoute(10.0));
  const nlohmann::json j = nlohmann::json::parse(io::routeToJsonString(res));

  EXPECT_EQ(j.at("route_id"), "drive");
  ASSERT_EQ(j.at("samples").size(), res.samples.size());
  for (std::size_t i = 0; i < res.samples.size(); ++i)
    EXPECT_EQ(j.at("samples")[i].at("index").get<int>(),
              static_cast<int>(i));
}

// --- SINR along a route ------------------------------------------------------

TEST(RouteSimulation, SinrFieldsPopulatedWhenEnabled) {
  Scene scene = openFieldScene();
  SimulationSettings s;
  s.maxReflections = 0;
  s.enableSinr = true;
  s.noiseBandwidthHz = 20e6;
  s.noiseFigureDb = 7.0;
  Simulator sim(s);

  const RouteResult res = sim.runRoute(scene, straightRoute(10.0));
  ASSERT_FALSE(res.samples.empty());
  for (const auto& smp : res.samples) {
    EXPECT_EQ(smp.servingTransmitterId, "tx0");
    EXPECT_TRUE(std::isfinite(smp.sinrDb));
  }

  // CSV now carries the SINR columns.
  const std::string csv = io::routeToCsvString(res);
  EXPECT_NE(csv.find("sinr_db"), std::string::npos);
}
