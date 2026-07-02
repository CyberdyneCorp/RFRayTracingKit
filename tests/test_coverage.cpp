#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "rftrace/exporters/csv_exporter.hpp"
#include "rftrace/exporters/json_exporter.hpp"
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
}  // namespace

TEST(Coverage, GridSizingAndCenumeration) {
  CoverageGrid g;
  g.origin = {0, 0, 0};
  g.cellSize = 10.0;
  g.cols = 5;
  g.rows = 4;
  g.height = 1.5;
  EXPECT_EQ(g.cellCount(), 20);
  const Vec3 c = g.cellCenter(0, 0);
  EXPECT_NEAR(c.x(), 5.0, 1e-9);
  EXPECT_NEAR(c.y(), 5.0, 1e-9);
  EXPECT_NEAR(c.z(), 1.5, 1e-9);
}

TEST(Coverage, EmptySceneAllCellsCovered) {
  Scene scene;
  scene.addTransmitter(tx({50, 50, 30}));

  CoverageGrid g;
  g.origin = {0, 0, 0};
  g.cellSize = 25.0;
  g.cols = 4;
  g.rows = 4;
  g.height = 1.5;

  SimulationSettings s;
  s.maxReflections = 0;
  const CoverageResult cov = Simulator(s).runCoverage(scene, g);

  EXPECT_EQ(static_cast<int>(cov.powerDbm.size()), 16);
  for (double p : cov.powerDbm) EXPECT_TRUE(std::isfinite(p));
  // Georeference retained.
  EXPECT_EQ(cov.grid.cols, 4);
  EXPECT_EQ(cov.grid.rows, 4);
  EXPECT_NEAR(cov.grid.cellSize, 25.0, 1e-9);
}

TEST(Coverage, BlockedCellsAreNoSignal) {
  Scene scene;
  scene.addTransmitter(tx({0, 0, 10}));
  // Wall at x=50 blocking cells behind it.
  scene.addMesh({Triangle{{50, -100, 0}, {50, 100, 0}, {50, 100, 100}},
                 Triangle{{50, -100, 0}, {50, 100, 100}, {50, -100, 100}}},
                "");

  CoverageGrid g;
  g.origin = {0, -10, 0};
  g.cellSize = 20.0;
  g.cols = 6;  // x centers: 10,30,50,70,90,110
  g.rows = 1;
  g.height = 10.0;

  SimulationSettings s;
  s.maxReflections = 0;
  const CoverageResult cov = Simulator(s).runCoverage(scene, g);

  int finite = 0, noSignal = 0;
  for (double p : cov.powerDbm) (std::isfinite(p) ? finite : noSignal)++;
  EXPECT_GT(finite, 0);
  EXPECT_GT(noSignal, 0);  // cells behind the wall
}

TEST(Coverage, JsonExportContainsGridAndValues) {
  Scene scene;
  scene.addTransmitter(tx({20, 20, 20}));
  CoverageGrid g;
  g.cellSize = 20.0;
  g.cols = 3;
  g.rows = 3;
  SimulationSettings s;
  s.maxReflections = 0;
  const CoverageResult cov = Simulator(s).runCoverage(scene, g);

  const std::string j = io::coverageToJsonString(cov);
  EXPECT_NE(j.find("\"grid\""), std::string::npos);
  EXPECT_NE(j.find("\"power_dbm\""), std::string::npos);
  EXPECT_NE(j.find("\"cell_size\""), std::string::npos);
}

TEST(Coverage, CsvIsLongTableOneRowPerCell) {
  Scene scene;
  scene.addTransmitter(tx({20, 20, 20}));
  CoverageGrid g;
  g.cellSize = 20.0;
  g.cols = 3;
  g.rows = 2;
  SimulationSettings s;
  s.maxReflections = 0;
  const CoverageResult cov = Simulator(s).runCoverage(scene, g);

  const std::string csv = io::coverageToCsvString(cov);
  EXPECT_EQ(csv.rfind("row,col,x,y,power", 0), 0u);  // header first
  const auto rows = std::count(csv.begin(), csv.end(), '\n');
  EXPECT_EQ(rows, 1 + g.cellCount());  // header + one row per cell
}
