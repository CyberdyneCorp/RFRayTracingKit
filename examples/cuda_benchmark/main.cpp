// CPU-vs-CUDA traversal benchmark on a large procedural "city" scene.
//
// Builds a grid of boxes (12 triangles each), then times each backend for:
//   1. build()            — acceleration-structure construction (BVH vs OptiX GAS)
//   2. closestHitBatch()  — a batch of random closest-hit queries
//   3. occludedBatch()    — a batch of random occlusion (segment) queries
// and reports wall-clock time, throughput (Mrays/s), and the CUDA/CPU speedup.
//
// A correctness cross-check compares a sample of results so the speedup reflects
// real, agreeing traversal (float GPU vs double CPU) rather than a backend that
// silently did less work. The CUDA backend needs an NVIDIA GPU + OptiX at
// runtime; without one the tool reports CUDA unavailable and runs the CPU side
// only. Usage: rftrace_cuda_benchmark [targetTriangles] [rayCount] [seed]

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "rftrace/backend.hpp"
#include "rftrace/geometry.hpp"
#include "rftrace/math.hpp"

using namespace rftrace;
using Clock = std::chrono::steady_clock;

namespace {

double secondsSince(Clock::time_point t0) {
  return std::chrono::duration<double>(Clock::now() - t0).count();
}

// Append the 12 triangles of the axis-aligned box [lo, hi].
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

// A grid of buildings with pseudo-random footprints and heights on a fixed
// street pitch — a stand-in for a dense urban scene.
std::vector<Triangle> makeCity(int nx, int ny, unsigned seed) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<double> height(15.0, 90.0);
  std::uniform_real_distribution<double> inset(1.0, 4.0);
  constexpr double pitch = 20.0;  // metres between building centres

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

struct Box {
  Vec3 lo, hi;
};

Box sceneBounds(const std::vector<Triangle>& tris) {
  Vec3 lo = tris.front().v0, hi = tris.front().v0;
  for (const Triangle& t : tris)
    for (const Vec3* v : {&t.v0, &t.v1, &t.v2}) {
      lo = lo.cwiseMin(*v);
      hi = hi.cwiseMax(*v);
    }
  return {lo, hi};
}

// Rays that start above the city and shoot toward random ground targets, so
// most rays actually strike geometry (a realistic, hit-heavy workload).
std::vector<Ray> makeRays(std::size_t n, unsigned seed, const Box& b) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<double> dx(b.lo.x(), b.hi.x());
  std::uniform_real_distribution<double> dy(b.lo.y(), b.hi.y());
  const double top = b.hi.z() + 40.0;
  std::vector<Ray> rays;
  rays.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const Vec3 from(dx(gen), dy(gen), top);
    const Vec3 to(dx(gen), dy(gen), 0.0);
    Vec3 dir = to - from;
    const double len = dir.norm();
    dir = len > 0.0 ? (dir / len) : Vec3(0, 0, -1);
    rays.push_back(Ray(from, dir));
  }
  return rays;
}

std::vector<Ray> makeSegments(std::size_t n, unsigned seed, const Box& b) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<double> dx(b.lo.x(), b.hi.x());
  std::uniform_real_distribution<double> dy(b.lo.y(), b.hi.y());
  std::uniform_real_distribution<double> dz(b.lo.z(), b.hi.z());
  std::vector<Ray> rays;
  rays.reserve(n);
  for (std::size_t i = 0; i < n; ++i)
    rays.push_back(segmentRay(Vec3(dx(gen), dy(gen), dz(gen)),
                              Vec3(dx(gen), dy(gen), dz(gen))));
  return rays;
}

double mraysPerSec(std::size_t rays, double seconds) {
  return seconds > 0.0 ? (rays / seconds) / 1e6 : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::size_t targetTris = argc > 1 ? std::strtoull(argv[1], nullptr, 10)
                                          : 300000;
  const std::size_t rayCount = argc > 2 ? std::strtoull(argv[2], nullptr, 10)
                                        : 1000000;
  const unsigned seed = argc > 3 ? static_cast<unsigned>(std::atoi(argv[3])) : 1;

  // Choose a near-square grid so buildings*12 ≈ targetTriangles.
  const int side = std::max(
      1, static_cast<int>(std::lround(std::sqrt(targetTris / 12.0))));
  const std::vector<Triangle> tris = makeCity(side, side, seed);
  const Box bounds = sceneBounds(tris);

  std::printf("Scene: %dx%d buildings = %zu triangles\n", side, side,
              tris.size());
  std::printf("Rays:  %zu closest-hit + %zu occlusion\n\n", rayCount, rayCount);

  const std::vector<Ray> rays = makeRays(rayCount, seed + 7, bounds);
  const std::vector<Ray> segs = makeSegments(rayCount, seed + 13, bounds);

  const bool haveCuda = backendAvailable(Backend::CUDA);
  if (!haveCuda)
    std::printf("[CUDA unavailable at runtime — running CPU only]\n\n");

  auto cpu = makeBackend(Backend::CPU, false);
  std::unique_ptr<IBackend> gpu =
      haveCuda ? makeBackend(Backend::CUDA, false) : nullptr;

  // ---- build() -------------------------------------------------------------
  auto t0 = Clock::now();
  cpu->build(tris);
  const double cpuBuild = secondsSince(t0);
  double gpuBuild = 0.0;
  if (gpu) {
    t0 = Clock::now();
    gpu->build(tris);
    gpuBuild = secondsSince(t0);
  }

  // Warm up the GPU pipeline (first launch carries one-time cost) off the clock.
  if (gpu) {
    const std::vector<Ray> warm(rays.begin(),
                                rays.begin() + std::min<std::size_t>(1024, rayCount));
    gpu->closestHitBatch(warm);
  }

  // ---- closestHitBatch() ---------------------------------------------------
  t0 = Clock::now();
  const std::vector<Hit> cpuHits = cpu->closestHitBatch(rays);
  const double cpuClosest = secondsSince(t0);
  std::vector<Hit> gpuHits;
  double gpuClosest = 0.0;
  if (gpu) {
    t0 = Clock::now();
    gpuHits = gpu->closestHitBatch(rays);
    gpuClosest = secondsSince(t0);
  }

  // ---- occludedBatch() -----------------------------------------------------
  t0 = Clock::now();
  const std::vector<char> cpuOcc = cpu->occludedBatch(segs);
  const double cpuOccT = secondsSince(t0);
  std::vector<char> gpuOcc;
  double gpuOccT = 0.0;
  if (gpu) {
    t0 = Clock::now();
    gpuOcc = gpu->occludedBatch(segs);
    gpuOccT = secondsSince(t0);
  }

  // ---- correctness cross-check --------------------------------------------
  std::size_t cpuHitN = 0, gpuHitN = 0, hitMissAgree = 0, triAgree = 0;
  for (std::size_t i = 0; i < rayCount; ++i) {
    if (cpuHits[i].valid) ++cpuHitN;
    if (gpu && gpuHits[i].valid) {
      ++gpuHitN;
      if (cpuHits[i].valid && cpuHits[i].triangle == gpuHits[i].triangle)
        ++triAgree;
    }
    if (gpu && cpuHits[i].valid == gpuHits[i].valid) ++hitMissAgree;
  }
  std::size_t occAgree = 0, cpuOccN = 0;
  for (std::size_t i = 0; i < rayCount; ++i) {
    if (cpuOcc[i]) ++cpuOccN;
    if (gpu && (cpuOcc[i] != 0) == (gpuOcc[i] != 0)) ++occAgree;
  }

  // ---- report --------------------------------------------------------------
  auto row = [&](const char* name, double cpuT, double gpuT, std::size_t n) {
    std::printf("%-14s  CPU %8.3f s (%6.1f Mray/s)", name, cpuT,
                mraysPerSec(n, cpuT));
    if (gpu)
      std::printf("   GPU %8.4f s (%7.1f Mray/s)   speedup %6.1fx", gpuT,
                  mraysPerSec(n, gpuT), gpuT > 0 ? cpuT / gpuT : 0.0);
    std::printf("\n");
  };

  std::printf("---- timings ----\n");
  std::printf("%-14s  CPU %8.3f s%s", "build", cpuBuild, gpu ? "" : "\n");
  if (gpu)
    std::printf("                    GPU %8.4f s                    speedup %6.1fx\n",
                gpuBuild, gpuBuild > 0 ? cpuBuild / gpuBuild : 0.0);
  row("closest-hit", cpuClosest, gpuClosest, rayCount);
  row("occlusion", cpuOccT, gpuOccT, rayCount);

  std::printf("\n---- workload / correctness ----\n");
  std::printf("closest-hit rate:  CPU %.1f%%", 100.0 * cpuHitN / rayCount);
  if (gpu) {
    std::printf("   GPU %.1f%%\n", 100.0 * gpuHitN / rayCount);
    std::printf("hit/miss agreement: %.3f%%   triangle agreement (both hit): %.3f%%\n",
                100.0 * hitMissAgree / rayCount,
                gpuHitN ? 100.0 * triAgree / gpuHitN : 0.0);
  } else {
    std::printf("\n");
  }
  std::printf("occlusion rate:    CPU %.1f%%", 100.0 * cpuOccN / rayCount);
  if (gpu)
    std::printf("   occlusion agreement: %.3f%%",
                100.0 * occAgree / rayCount);
  std::printf("\n");
  return 0;
}
