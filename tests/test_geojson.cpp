#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "rftrace/exporters/geojson_exporter.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;
using nlohmann::json;

namespace {
RFResult reflectionResult() {
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
  return Simulator(s).run(scene);
}

void expectValidFeatureCollection(const json& fc) {
  ASSERT_EQ(fc.at("type"), "FeatureCollection");
  ASSERT_TRUE(fc.at("features").is_array());
  ASSERT_FALSE(fc.at("features").empty());
  for (const auto& f : fc.at("features")) {
    EXPECT_EQ(f.at("type"), "Feature");
    ASSERT_TRUE(f.contains("geometry"));
    EXPECT_TRUE(f.at("geometry").contains("type"));
    EXPECT_TRUE(f.at("geometry").contains("coordinates"));
    EXPECT_TRUE(f.contains("properties"));
  }
}
}  // namespace

TEST(GeoJson, ReceiversAreValidPointFeatures) {
  const json fc = json::parse(io::receiversToGeoJsonString(reflectionResult()));
  expectValidFeatureCollection(fc);
  EXPECT_EQ(fc.at("features").at(0).at("geometry").at("type"), "Point");
  EXPECT_TRUE(fc.at("features").at(0).at("properties").contains(
      "received_power_dbm"));
}

TEST(GeoJson, PathsAreValidLineFeatures) {
  const json fc = json::parse(io::pathsToGeoJsonString(reflectionResult()));
  expectValidFeatureCollection(fc);
  bool sawLine = false;
  for (const auto& f : fc.at("features"))
    if (f.at("geometry").at("type") == "LineString") {
      sawLine = true;
      EXPECT_TRUE(f.at("properties").contains("power_dbm"));
      EXPECT_TRUE(f.at("properties").contains("reflections"));
    }
  EXPECT_TRUE(sawLine);
}

TEST(GeoJson, CoverageCellsAreValidPolygons) {
  Scene scene;
  Transmitter tx;
  tx.id = "tx";
  tx.position = {20, 20, 20};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);
  CoverageGrid g;
  g.cellSize = 20.0;
  g.cols = 3;
  g.rows = 3;
  SimulationSettings s;
  s.maxReflections = 0;
  const CoverageResult cov = Simulator(s).runCoverage(scene, g);

  const json fc = json::parse(io::coverageToGeoJsonString(cov));
  expectValidFeatureCollection(fc);
  EXPECT_EQ(fc.at("features").at(0).at("geometry").at("type"), "Polygon");
}
