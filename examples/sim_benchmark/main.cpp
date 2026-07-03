// Full-simulation CPU-vs-CUDA benchmark: times end-to-end Simulator::runCoverage
// on a procedural city, exercising the batched simulator path (Phase 1 batched
// LOS occlusion in image-method coverage; Phase 2 batched ray-launch wavefront).
// Unlike the low-level rftrace_cuda_benchmark (which times raw batch dispatch),
// this measures what a user actually pays for a whole coverage run — including
// building the backend acceleration structure once per call. It also reports the
// backend-build (OptiX context + GAS) cost separately, since that fixed overhead
// is amortized differently across the two modes. Runs CPU-only when no CUDA
// device is present. Usage: rftrace_sim_benchmark [buildingsPerSide] [gridSide]

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "rftrace/backend.hpp"
#include "rftrace/scene.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;
using Clock = std::chrono::steady_clock;

namespace {

double secondsSince(Clock::time_point t0) {
  return std::chrono::duration<double>(Clock::now() - t0).count();
}

void appendBox(std::vector<Triangle>& tris, const Vec3& lo, const Vec3& hi) {
  const Vec3 p[8] = {{lo.x(), lo.y(), lo.z()}, {hi.x(), lo.y(), lo.z()},
                     {hi.x(), hi.y(), lo.z()}, {lo.x(), hi.y(), lo.z()},
                     {lo.x(), lo.y(), hi.z()}, {hi.x(), lo.y(), hi.z()},
                     {hi.x(), hi.y(), hi.z()}, {lo.x(), hi.y(), hi.z()}};
  const int f[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
                        {0, 4, 5}, {0, 5, 1}, {3, 2, 6}, {3, 6, 7},
                        {1, 5, 6}, {1, 6, 2}, {0, 3, 7}, {0, 7, 4}};
  for (const auto& t : f) tris.push_back({p[t[0]], p[t[1]], p[t[2]]});
}

std::vector<Triangle> makeCity(int nx, int ny, unsigned seed) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<double> height(15.0, 90.0);
  std::uniform_real_distribution<double> inset(1.0, 4.0);
  constexpr double pitch = 20.0;
  std::vector<Triangle> tris;
  tris.reserve(static_cast<std::size_t>(nx) * ny * 12);
  for (int i = 0; i < nx; ++i)
    for (int j = 0; j < ny; ++j) {
      const double x = i * pitch, y = j * pitch;
      const double dx = inset(gen), dy = inset(gen);
      appendBox(tris, Vec3(x + dx, y + dy, 0.0),
                Vec3(x + pitch - dx, y + pitch - dy, height(gen)));
    }
  return tris;
}

// Fraction of cells where the two coverage maps agree: same signal/no-signal,
// and (where both have signal) power within `tolDb`.
double agreement(const CoverageResult& a, const CoverageResult& b, double tolDb) {
  std::size_t agree = 0;
  const std::size_t n = a.powerDbm.size();
  for (std::size_t i = 0; i < n; ++i) {
    const bool as = std::isfinite(a.powerDbm[i]);
    const bool bs = std::isfinite(b.powerDbm[i]);
    if (as != bs) continue;
    if (!as || std::abs(a.powerDbm[i] - b.powerDbm[i]) <= tolDb) ++agree;
  }
  return n ? 100.0 * agree / n : 0.0;
}

CoverageResult runCov(const Scene& scene, const CoverageGrid& grid, Backend b,
                      PropagationMode mode, int maxRefl, int rays) {
  SimulationSettings s;
  s.backend = b;
  s.allowBackendFallback = false;
  s.mode = mode;
  s.maxReflections = maxRefl;
  s.raysPerTransmitter = rays;
  s.captureRadius = 3.0;
  return Simulator(s).runCoverage(scene, grid);
}

}  // namespace

int main(int argc, char** argv) {
  const int side = argc > 1 ? std::atoi(argv[1]) : 30;   // buildings per side
  const int gridSide = argc > 2 ? std::atoi(argv[2]) : 160;  // cells per side

  Scene scene;
  const std::vector<Triangle> city = makeCity(side, side, 1);
  scene.addMesh(city, -1);
  const double span = side * 20.0;
  Transmitter t;
  t.id = "tx";
  t.position = Vec3(span * 0.5, span * 0.5, 120.0);
  t.frequencyHz = 3.5e9;
  t.powerDbm = 46.0;
  scene.addTransmitter(t);

  CoverageGrid grid;
  grid.origin = Vec3(0, 0, 0);
  grid.cellSize = span / gridSide;
  grid.cols = gridSide;
  grid.rows = gridSide;
  grid.height = 1.5;

  const bool haveCuda = backendAvailable(Backend::CUDA);
  std::printf("Scene: %d buildings (%zu triangles), %dx%d = %d coverage cells\n",
              side * side, city.size(), gridSide, gridSide, grid.cellCount());
  if (!haveCuda) std::printf("[CUDA unavailable — CPU only]\n");
  std::printf("\n");

  // Backend-build (OptiX context + GAS) cost, measured once — this fixed cost is
  // paid inside every runCoverage call (the simulator builds the backend per run).
  if (haveCuda) {
    auto t0 = Clock::now();
    auto gpu = makeBackend(Backend::CUDA, false);
    gpu->build(scene.triangles());
    std::printf("CUDA backend build (OptiX ctx + GAS), one-time: %.3f s\n\n",
                secondsSince(t0));
  }

  struct Case {
    const char* name;
    PropagationMode mode;
    int maxRefl;
    int rays;
  };
  const Case cases[] = {
      {"coverage LOS (image, 0 refl)", PropagationMode::ImageMethod, 0, 0},
      {"coverage ray-launch (2 refl)", PropagationMode::RayLaunch, 2, 40000},
  };

  std::printf("%-32s  %10s  %12s  %8s  %8s\n", "mode", "CPU (s)", "CUDA (s)",
              "speedup", "agree%");
  for (const Case& c : cases) {
    auto t0 = Clock::now();
    const CoverageResult cpu =
        runCov(scene, grid, Backend::CPU, c.mode, c.maxRefl, c.rays);
    const double cpuT = secondsSince(t0);

    if (!haveCuda) {
      std::printf("%-32s  %10.3f  %12s\n", c.name, cpuT, "-");
      continue;
    }
    t0 = Clock::now();
    const CoverageResult gpu =
        runCov(scene, grid, Backend::CUDA, c.mode, c.maxRefl, c.rays);
    const double gpuT = secondsSince(t0);
    // LOS/image geometry is host-double, so cells agree exactly; ray-launch uses
    // float hit points on GPU, so allow a small power tolerance.
    const double tol = c.mode == PropagationMode::ImageMethod ? 1e-6 : 1.0;
    std::printf("%-32s  %10.3f  %12.3f  %7.1fx  %7.1f\n", c.name, cpuT, gpuT,
                gpuT > 0 ? cpuT / gpuT : 0.0, agreement(cpu, gpu, tol));
  }
  std::printf(
      "\nNote: each runCoverage builds its backend once; for CUDA that includes"
      "\nthe OptiX context/GAS cost above. LOS-only coverage is cheap on CPU, so"
      "\nthe GPU wins only when the per-run ray work outweighs that fixed cost.\n");
  return 0;
}
