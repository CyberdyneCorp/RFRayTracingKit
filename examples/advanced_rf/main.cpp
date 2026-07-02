// advanced_rf — Phase 7 features: diffraction over a blocking wall, a two-cell
// SINR coverage map, and a drive-test route with CSV export.
#include <iostream>

#include "rftrace/rftrace.hpp"
#include "rftrace/route.hpp"

using namespace rftrace;

namespace {
Transmitter tx(const std::string& id, const Vec3& p) {
  Transmitter t;
  t.id = id;
  t.position = p;
  t.frequencyHz = 3.5e9;
  t.powerDbm = 43.0;
  return t;
}
}  // namespace

int main() {
  // --- 1. Diffraction over a blocking wall -----------------------------------
  {
    Scene scene;
    scene.addMaterial(materials::preset("concrete"));
    // Tall wall in the x=0 plane blocking the direct path.
    scene.addMesh({Triangle{{0, -40, 0}, {0, 40, 0}, {0, 40, 30}},
                   Triangle{{0, -40, 0}, {0, 40, 30}, {0, -40, 30}}},
                  "concrete");
    scene.addTransmitter(tx("bs", {-50, 0, 5}));
    Receiver rx;
    rx.id = "shadow";
    rx.position = {50, 0, 5};  // geometric shadow behind the wall
    scene.addReceiver(rx);

    SimulationSettings s;
    s.maxReflections = 0;
    s.enableDiffraction = true;
    const RFResult r = Simulator(s).run(scene);
    const auto* rr = r.receiver("shadow");
    int diff = 0;
    for (const auto& p : rr->paths)
      if (p.type == PathType::Diffraction) ++diff;
    std::cout << "[diffraction] shadowed rx: " << rr->paths.size()
              << " path(s), " << diff << " diffracted, power="
              << (rr->hasSignal ? rr->receivedPowerDbm : 0.0) << " dBm\n";
  }

  // --- 2. Two-cell SINR coverage map -----------------------------------------
  {
    Scene scene;
    scene.addTransmitter(tx("cell_a", {20, 50, 30}));
    scene.addTransmitter(tx("cell_b", {180, 50, 30}));

    CoverageGrid grid;
    grid.origin = {0, 0, 0};
    grid.cellSize = 10.0;
    grid.cols = 20;
    grid.rows = 10;
    grid.height = 1.5;

    SimulationSettings s;
    s.maxReflections = 0;
    s.enableSinr = true;
    s.noiseBandwidthHz = 20e6;
    s.noiseFigureDb = 7.0;
    const CoverageResult cov = Simulator(s).runCoverage(scene, grid);
    io::exportCoverageJson(cov, "advanced_coverage.json");

    double bestSinr = -1e30;
    int covered = 0;
    for (double v : cov.sinrDb)
      if (std::isfinite(v)) { ++covered; bestSinr = std::max(bestSinr, v); }
    std::cout << "[sinr] two-cell coverage " << grid.cols << "x" << grid.rows
              << ": " << covered << " cells, peak SINR=" << bestSinr
              << " dB (advanced_coverage.json)\n";
  }

  // --- 3. Drive-test route ----------------------------------------------------
  {
    Scene scene;
    scene.addTransmitter(tx("bs", {0, 0, 30}));  // route carries its own receivers

    Route route;
    route.id = "drive";
    route.waypoints = {{20, 0, 1.5}, {120, 0, 1.5}, {120, 80, 1.5}};
    route.sampleSpacing = 10.0;

    SimulationSettings s;
    s.maxReflections = 0;
    const RouteResult rr = Simulator(s).runRoute(scene, route);
    io::exportRouteCsv(rr, "drive_test.csv");
    std::cout << "[route] " << rr.samples.size()
              << " samples along the route (drive_test.csv)\n";

    if (rr.samples.empty()) {
      std::cout << "FAIL: empty route result\n";
      return 1;
    }
  }

  return 0;
}
