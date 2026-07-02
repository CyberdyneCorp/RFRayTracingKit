// coverage_grid — one transmitter over a wall, evaluated on a coverage grid,
// exported to CSV / JSON / GeoJSON and debug ray paths to glTF.
#include <iostream>

#include "rftrace/rftrace.hpp"

int main() {
  using namespace rftrace;

  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  // A building wall the signal reflects off / is blocked by.
  scene.addMesh({Triangle{{60, 40, 0}, {60, 120, 0}, {60, 120, 40}},
                 Triangle{{60, 40, 0}, {60, 120, 40}, {60, 40, 40}}},
                "concrete");

  Transmitter tx;
  tx.id = "tower_1";
  tx.position = {20.0, 80.0, 30.0};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);

  CoverageGrid grid;
  grid.origin = {0.0, 0.0, 0.0};
  grid.cellSize = 5.0;
  grid.cols = 40;
  grid.rows = 32;
  grid.height = 1.5;

  SimulationSettings settings;
  settings.maxReflections = 1;
  settings.simulationId = "coverage_grid";

  Simulator sim(settings);
  const CoverageResult cov = sim.runCoverage(scene, grid);

  int covered = 0;
  double best = CoverageResult::NoSignal;
  for (double p : cov.powerDbm)
    if (std::isfinite(p)) {
      ++covered;
      best = std::max(best, p);
    }

  io::exportCoverageCsv(cov, "coverage.csv");
  io::exportCoverageJson(cov, "coverage.json");
  io::exportCoverageGeoJson(cov, "coverage.geojson");

  std::cout << "coverage " << grid.cols << "x" << grid.rows << ": " << covered
            << "/" << grid.cellCount() << " cells covered, peak " << best
            << " dBm  (exported coverage.csv/.json/.geojson)\n";

  if (covered == 0) {
    std::cout << "FAIL: no cell covered\n";
    return 1;
  }
  return 0;
}
