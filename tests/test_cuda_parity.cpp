// CPU-vs-CUDA parity test suite. Built only when RFTRACE_HAVE_CUDA is defined
// (RFTRACE_ENABLE_CUDA=ON with the CUDA Toolkit + OptiX SDK found at configure
// time); every test skips at runtime when no CUDA device is present, so the
// default CI is unaffected and the file compiles to a no-op in the default build.
//
// The OptiX/CUDA acceleration structure and traversal work in float32 while the
// CPU reference (the BVH) works in double. Parity therefore compares hit/miss
// and triangle index exactly for well-separated geometry, and t within a
// tolerance (D4). Where float32 rounding legitimately flips a result — a ray
// grazing a triangle edge, sitting on the [tMin,tMax] boundary, or a near-tie
// between two triangles at almost the same depth — the disagreement is
// classified as "borderline" (verified against the exact double-precision
// geometry) rather than counted as a failure. Genuine traversal bugs produce
// large, non-borderline discrepancies and are asserted to be zero. This mirrors
// the Metal and OpenCL parity suites.
//
// Verified on an NVIDIA RTX 5060 (Blackwell, sm_120) with OptiX SDK 9.0.0: every
// test below passes there. It requires an NVIDIA GPU with OptiX; on any other host
// the tests GTEST_SKIP at runtime.
#if RFTRACE_HAVE_CUDA

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "rftrace/backend.hpp"
#include "rftrace/geometry.hpp"
#include "rftrace/math.hpp"
#include "rftrace/scene.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {

// ---- Tolerances -------------------------------------------------------------
constexpr double kAbsT = 1e-2;         // absolute t tolerance in metres (D4)
constexpr double kRelT = 1e-4;         // relative term for far hits
constexpr double kEdgeEps = 2e-3;      // barycentric grazing threshold
constexpr double kBoundaryEps = 2e-2;  // distance (m) to an interval endpoint
constexpr double kTieTol = 5e-2;       // depth gap (m) below which index may flip

bool tClose(double a, double b) {
  return std::abs(a - b) <= std::max(kAbsT, kRelT * std::abs(a));
}

double minBary(double u, double v) {
  return std::min({u, v, 1.0 - u - v});
}

// ---- Scene builders ---------------------------------------------------------

// A wall in the x=5 plane spanning y,z in [0,4], split by the diagonal y+z=4 so
// interior points are well-separated for a deterministic triangle-index check.
std::vector<Triangle> makeWall() {
  return {
      {Vec3(5, 0, 0), Vec3(5, 4, 0), Vec3(5, 0, 4)},  // tri 0: y+z < 4
      {Vec3(5, 4, 0), Vec3(5, 4, 4), Vec3(5, 0, 4)},  // tri 1: y+z > 4
  };
}

// The 12 triangles of an axis-aligned box [lo,hi].
std::vector<Triangle> makeBox(const Vec3& lo, const Vec3& hi) {
  const Vec3 p[8] = {
      {lo.x(), lo.y(), lo.z()}, {hi.x(), lo.y(), lo.z()},
      {hi.x(), hi.y(), lo.z()}, {lo.x(), hi.y(), lo.z()},
      {lo.x(), lo.y(), hi.z()}, {hi.x(), lo.y(), hi.z()},
      {hi.x(), hi.y(), hi.z()}, {lo.x(), hi.y(), hi.z()}};
  const int f[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
                        {0, 4, 5}, {0, 5, 1}, {3, 2, 6}, {3, 6, 7},
                        {1, 5, 6}, {1, 6, 2}, {0, 3, 7}, {0, 7, 4}};
  std::vector<Triangle> tris;
  tris.reserve(12);
  for (const auto& t : f) tris.push_back({p[t[0]], p[t[1]], p[t[2]]});
  return tris;
}

// Two buildings straddling a canyon along the y axis (gap in x = [-2,2]).
std::vector<Triangle> makeCanyon() {
  std::vector<Triangle> tris = makeBox(Vec3(-12, 0, 0), Vec3(-2, 40, 20));
  const std::vector<Triangle> right = makeBox(Vec3(2, 0, 0), Vec3(12, 40, 20));
  tris.insert(tris.end(), right.begin(), right.end());
  return tris;
}

// A few hundred small, randomly-placed triangles in a [15,85]^3 box.
std::vector<Triangle> makeRandomTriangles(int n, unsigned seed) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<double> center(15.0, 85.0);
  std::uniform_real_distribution<double> off(-3.0, 3.0);
  std::vector<Triangle> tris;
  tris.reserve(n);
  for (int i = 0; i < n; ++i) {
    const Vec3 c(center(gen), center(gen), center(gen));
    auto vert = [&]() -> Vec3 {
      return c + Vec3(off(gen), off(gen), off(gen));
    };
    tris.push_back({vert(), vert(), vert()});
  }
  return tris;
}

// ---- Ray builders (deterministic) ------------------------------------------

struct Box {
  Vec3 lo, hi;
};

Box paddedBounds(const std::vector<Triangle>& tris) {
  if (tris.empty()) return {Vec3(5, 5, 5), Vec3(95, 95, 95)};
  Vec3 lo = tris.front().v0, hi = tris.front().v0;
  for (const Triangle& t : tris)
    for (const Vec3* v : {&t.v0, &t.v1, &t.v2}) {
      lo = lo.cwiseMin(*v);
      hi = hi.cwiseMax(*v);
    }
  const double margin = 0.5 * (hi - lo).maxCoeff() + 3.0;
  return {lo - Vec3(margin, margin, margin), hi + Vec3(margin, margin, margin)};
}

std::vector<Ray> makeRandomRays(std::size_t n, unsigned seed, const Box& b) {
  std::mt19937 gen(seed);
  auto point = [&] {
    std::uniform_real_distribution<double> dx(b.lo.x(), b.hi.x());
    std::uniform_real_distribution<double> dy(b.lo.y(), b.hi.y());
    std::uniform_real_distribution<double> dz(b.lo.z(), b.hi.z());
    return Vec3(dx(gen), dy(gen), dz(gen));
  };
  std::vector<Ray> rays;
  rays.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const Vec3 a = point();
    Vec3 dir = point() - a;
    const double len = dir.norm();
    dir = len > 0.0 ? (dir / len) : Vec3(0, 0, 1);
    rays.push_back(Ray(a, dir));
  }
  return rays;
}

std::vector<Ray> makeRandomSegments(std::size_t n, unsigned seed, const Box& b) {
  std::mt19937 gen(seed);
  auto point = [&] {
    std::uniform_real_distribution<double> dx(b.lo.x(), b.hi.x());
    std::uniform_real_distribution<double> dy(b.lo.y(), b.hi.y());
    std::uniform_real_distribution<double> dz(b.lo.z(), b.hi.z());
    return Vec3(dx(gen), dy(gen), dz(gen));
  };
  std::vector<Ray> rays;
  rays.reserve(n);
  for (std::size_t i = 0; i < n; ++i)
    rays.push_back(segmentRay(point(), point()));
  return rays;
}

// ---- Exact-geometry analysis (double precision reference) -------------------

struct Analysis {
  Hit hit;
  bool nearTie = false;
  double edgeDist = 1.0;
  double boundaryGap = 1e9;
};

Analysis analyze(const std::vector<Triangle>& tris, const Ray& ray) {
  Analysis a;
  double bestT = std::numeric_limits<double>::infinity();
  double secondT = std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < tris.size(); ++i) {
    const Hit h = intersectTriangle(ray, tris[i], static_cast<int>(i));
    if (!h.valid) continue;
    if (h.t < bestT) {
      secondT = bestT;
      bestT = h.t;
      a.hit = h;
    } else if (h.t < secondT) {
      secondT = h.t;
    }
  }
  if (a.hit.valid) {
    a.nearTie = (secondT - bestT) < kTieTol;
    a.edgeDist = minBary(a.hit.u, a.hit.v);
    a.boundaryGap = std::min(bestT - ray.tMin, ray.tMax - bestT);
  }
  return a;
}

bool hasBorderlineIntersection(const std::vector<Triangle>& tris,
                               const Ray& ray) {
  for (const Triangle& tri : tris) {
    const Vec3 e1 = tri.edge1();
    const Vec3 e2 = tri.edge2();
    const Vec3 pvec = ray.direction.cross(e2);
    const double det = e1.dot(pvec);
    if (std::abs(det) < 1e-12) continue;
    const double invDet = 1.0 / det;
    const Vec3 tvec = ray.origin - tri.v0;
    const double u = tvec.dot(pvec) * invDet;
    const Vec3 qvec = tvec.cross(e1);
    const double v = ray.direction.dot(qvec) * invDet;
    const double t = e2.dot(qvec) * invDet;
    const double ed = minBary(u, v);
    if (ed < -kEdgeEps) continue;
    if (t < ray.tMin - kBoundaryEps) continue;
    if (t > ray.tMax + kBoundaryEps) continue;
    const double gapT = std::min(t - ray.tMin, ray.tMax - t);
    if (ed < kEdgeEps || gapT < kBoundaryEps) return true;
  }
  return false;
}

bool borderline(const std::vector<Triangle>& tris, const Ray& ray,
                const Analysis& a) {
  if (a.hit.valid && (a.edgeDist < kEdgeEps || a.boundaryGap < kBoundaryEps))
    return true;
  return hasBorderlineIntersection(tris, ray);
}

// ---- Aggregated comparison --------------------------------------------------

struct ParityStats {
  std::size_t rays = 0;
  std::size_t bothHit = 0;
  std::size_t strictIndexOk = 0;
  std::size_t indexMismatch = 0;
  std::size_t tMismatch = 0;
  std::size_t validHardMismatch = 0;
  std::size_t validSoftMismatch = 0;
  std::size_t occHardMismatch = 0;
  std::size_t occSoftMismatch = 0;
};

void compareClosest(const std::vector<Triangle>& tris,
                    const std::vector<Ray>& rays, const std::vector<Hit>& cpu,
                    const std::vector<Hit>& gpu, ParityStats& s) {
  for (std::size_t i = 0; i < rays.size(); ++i) {
    ++s.rays;
    const Hit& c = cpu[i];
    const Hit& g = gpu[i];
    if (c.valid && g.valid) {
      ++s.bothHit;
      if (!tClose(c.t, g.t)) ++s.tMismatch;
      if (c.triangle == g.triangle) {
        if (!analyze(tris, rays[i]).nearTie) ++s.strictIndexOk;
      } else if (!analyze(tris, rays[i]).nearTie) {
        ++s.indexMismatch;
      }
    } else if (c.valid != g.valid) {
      if (borderline(tris, rays[i], analyze(tris, rays[i])))
        ++s.validSoftMismatch;
      else
        ++s.validHardMismatch;
    }
  }
}

void compareOccluded(const std::vector<Triangle>& tris,
                     const std::vector<Ray>& rays, const std::vector<char>& cpu,
                     const std::vector<char>& gpu, ParityStats& s) {
  for (std::size_t i = 0; i < rays.size(); ++i) {
    if ((cpu[i] != 0) == (gpu[i] != 0)) continue;
    if (borderline(tris, rays[i], analyze(tris, rays[i])))
      ++s.occSoftMismatch;
    else
      ++s.occHardMismatch;
  }
}

ParityStats runScene(const std::vector<Triangle>& tris, unsigned seed,
                     std::size_t nClosest, std::size_t nOccluded) {
  auto cpu = makeBackend(Backend::CPU, false);
  auto gpu = makeBackend(Backend::CUDA, false);
  cpu->build(tris);
  gpu->build(tris);

  const Box box = paddedBounds(tris);
  const std::vector<Ray> hitRays = makeRandomRays(nClosest, seed, box);
  const std::vector<Ray> segRays = makeRandomSegments(nOccluded, seed + 1, box);

  ParityStats s;
  compareClosest(tris, hitRays, cpu->closestHitBatch(hitRays),
                 gpu->closestHitBatch(hitRays), s);
  compareOccluded(tris, segRays, cpu->occludedBatch(segRays),
                  gpu->occludedBatch(segRays), s);
  return s;
}

void expectParity(const ParityStats& s, bool expectHits = true) {
  EXPECT_EQ(s.indexMismatch, 0u) << "triangle-index disagreements (not ties)";
  EXPECT_EQ(s.tMismatch, 0u) << "t beyond tolerance";
  EXPECT_EQ(s.validHardMismatch, 0u) << "closest-hit/miss disagreements";
  EXPECT_EQ(s.occHardMismatch, 0u) << "occlusion disagreements";
  if (expectHits) {
    EXPECT_GT(s.bothHit, 0u) << "scene produced no hits";
    EXPECT_GT(s.strictIndexOk, 0u) << "no unambiguous index checks ran";
  }
}

constexpr std::size_t kClosestRays = 5000;
constexpr std::size_t kOccRays = 5000;

bool cudaReady() { return backendAvailable(Backend::CUDA); }

}  // namespace

TEST(CudaParityScenes, EmptyScene) {
  if (!cudaReady()) GTEST_SKIP() << "no CUDA device available";
  auto cpu = makeBackend(Backend::CPU, false);
  auto gpu = makeBackend(Backend::CUDA, false);
  const std::vector<Triangle> none;
  cpu->build(none);
  gpu->build(none);

  const Box box = paddedBounds(none);
  const std::vector<Ray> rays = makeRandomRays(1000, 1, box);
  const auto cpuHits = cpu->closestHitBatch(rays);
  const auto gpuHits = gpu->closestHitBatch(rays);
  ASSERT_EQ(gpuHits.size(), rays.size());
  for (std::size_t i = 0; i < rays.size(); ++i) {
    EXPECT_FALSE(cpuHits[i].valid);
    EXPECT_FALSE(gpuHits[i].valid) << "ray " << i;
  }
  const auto gpuOcc = gpu->occludedBatch(makeRandomSegments(1000, 2, box));
  for (char o : gpuOcc) EXPECT_EQ(o, 0);
}

TEST(CudaParityScenes, EmptyBatchReturnsEmpty) {
  if (!cudaReady()) GTEST_SKIP() << "no CUDA device available";
  auto gpu = makeBackend(Backend::CUDA, false);
  gpu->build(makeWall());
  EXPECT_TRUE(gpu->closestHitBatch({}).empty());
  EXPECT_TRUE(gpu->occludedBatch({}).empty());
}

TEST(CudaParityScenes, SingleWall) {
  if (!cudaReady()) GTEST_SKIP() << "no CUDA device available";
  const auto s = runScene(makeWall(), 100, kClosestRays, kOccRays);
  expectParity(s);
}

TEST(CudaParityScenes, TwoBuildingCanyon) {
  if (!cudaReady()) GTEST_SKIP() << "no CUDA device available";
  const auto s = runScene(makeCanyon(), 200, kClosestRays, kOccRays);
  expectParity(s);
}

TEST(CudaParityScenes, HundredsOfRandomTriangles) {
  if (!cudaReady()) GTEST_SKIP() << "no CUDA device available";
  const auto tris = makeRandomTriangles(300, 42);
  const auto s = runScene(tris, 300, kClosestRays, kOccRays);
  expectParity(s);
  EXPECT_LT(s.validSoftMismatch + s.occSoftMismatch, kClosestRays / 20)
      << "unexpectedly many borderline disagreements";
}

TEST(CudaParityScenes, ExplicitOccludedClearSegments) {
  if (!cudaReady()) GTEST_SKIP() << "no CUDA device available";
  auto cpu = makeBackend(Backend::CPU, false);
  auto gpu = makeBackend(Backend::CUDA, false);
  const auto wall = makeWall();
  cpu->build(wall);
  gpu->build(wall);
  const std::vector<Ray> rays = {
      Ray(Vec3(0, 1, 1), Vec3(1, 0, 0)),             // blocked by the wall
      Ray(Vec3(0, 3, 3), Vec3(1, 0, 0)),             // blocked by the wall
      Ray(Vec3(0, 1, 1), Vec3(-1, 0, 0)),            // clear: away from wall
      Ray(Vec3(0, 1, 1), Vec3(1, 0, 0), 1e-4, 3.0),  // clear: wall past tMax
  };
  const auto cpuOcc = cpu->occludedBatch(rays);
  const auto gpuOcc = gpu->occludedBatch(rays);
  ASSERT_EQ(gpuOcc.size(), rays.size());
  for (std::size_t i = 0; i < rays.size(); ++i)
    EXPECT_EQ(cpuOcc[i] != 0, gpuOcc[i] != 0) << "ray " << i;
}

// The CUDA dispatch must be deterministic: the same rays twice yield identical
// results (bit-for-bit on the reported fields).
TEST(CudaParityScenes, DeterministicAcrossRuns) {
  if (!cudaReady()) GTEST_SKIP() << "no CUDA device available";
  auto gpu = makeBackend(Backend::CUDA, false);
  const auto tris = makeRandomTriangles(300, 7);
  gpu->build(tris);
  const Box box = paddedBounds(tris);

  const std::vector<Ray> rays = makeRandomRays(4000, 9, box);
  const auto a = gpu->closestHitBatch(rays);
  const auto b = gpu->closestHitBatch(rays);
  ASSERT_EQ(a.size(), b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].valid, b[i].valid) << "ray " << i;
    EXPECT_EQ(a[i].triangle, b[i].triangle) << "ray " << i;
    EXPECT_EQ(a[i].t, b[i].t) << "ray " << i;
    EXPECT_EQ(a[i].u, b[i].u) << "ray " << i;
    EXPECT_EQ(a[i].v, b[i].v) << "ray " << i;
  }

  const std::vector<Ray> segs = makeRandomSegments(4000, 11, box);
  const auto oa = gpu->occludedBatch(segs);
  const auto ob = gpu->occludedBatch(segs);
  EXPECT_EQ(oa, ob);
}

// The caller-owned-output fast path (closestHitBatchInto / occludedBatchInto)
// must agree bit-for-bit with the vector-returning forms, and a reused buffer
// carrying a prior batch's hits must be fully overwritten on the next batch.
TEST(CudaParityScenes, CallerOwnedOutputMatchesVector) {
  if (!cudaReady()) GTEST_SKIP() << "no CUDA device available";
  auto gpu = makeBackend(Backend::CUDA, false);
  const auto tris = makeRandomTriangles(300, 21);
  gpu->build(tris);
  const Box box = paddedBounds(tris);

  const std::vector<Ray> rays = makeRandomRays(4000, 23, box);
  const std::vector<Hit> ref = gpu->closestHitBatch(rays);
  std::vector<Hit> into(rays.size());
  gpu->closestHitBatchInto(rays, into);
  ASSERT_EQ(into.size(), ref.size());
  for (std::size_t i = 0; i < ref.size(); ++i) {
    EXPECT_EQ(into[i].valid, ref[i].valid) << "ray " << i;
    EXPECT_EQ(into[i].triangle, ref[i].triangle) << "ray " << i;
    EXPECT_EQ(into[i].t, ref[i].t) << "ray " << i;
  }

  const std::vector<Ray> segs = makeRandomSegments(4000, 25, box);
  const std::vector<char> occRef = gpu->occludedBatch(segs);
  std::vector<char> occInto(segs.size());
  gpu->occludedBatchInto(segs, occInto);
  EXPECT_EQ(occInto, occRef);

  // Reuse `into` (full of hits) for an all-miss batch: no slot may stay valid.
  const std::vector<Ray> misses(rays.size(),
                                Ray(Vec3(1e6, 1e6, 1e6), Vec3(0, 0, 1)));
  gpu->closestHitBatchInto(misses, into);
  for (std::size_t i = 0; i < into.size(); ++i)
    EXPECT_FALSE(into[i].valid) << "stale hit at " << i;
}

// End-to-end validation: a full LOS coverage run on the CUDA backend must agree
// with the CPU backend. With maxReflections=0 the ONLY backend query is LOS
// occlusion (the received power is host-double FSPL), so wherever the float GPU
// traversal and the double CPU BVH agree on occlusion the cell power is
// bit-identical; only cells grazing a shadow edge may flip float-vs-double, and
// those are required to be few. This exercises the Phase-1 batched-LOS path
// through Simulator::runCoverage on real hardware.
TEST(CudaFullSim, LosCoverageAgreesWithCpu) {
  if (!cudaReady()) GTEST_SKIP() << "no CUDA device available";

  Scene scene;
  scene.addMesh(makeBox(Vec3(-30, -5, 0), Vec3(-10, 60, 25)), -1);
  scene.addMesh(makeBox(Vec3(10, -5, 0), Vec3(30, 60, 25)), -1);
  Transmitter t;
  t.id = "tx";
  t.position = Vec3(0, 30, 40);
  t.frequencyHz = 3.5e9;
  t.powerDbm = 43.0;
  scene.addTransmitter(t);

  CoverageGrid g;
  g.origin = Vec3(-50, -10, 0);
  g.cellSize = 2.0;
  g.cols = 50;
  g.rows = 40;
  g.height = 1.5;

  auto runOn = [&](Backend b) {
    SimulationSettings s;
    s.backend = b;
    s.allowBackendFallback = false;  // force the real CUDA backend, no CPU fallback
    s.maxReflections = 0;            // LOS-only: the one backend query is occlusion
    return Simulator(s).runCoverage(scene, g);
  };
  const CoverageResult cpu = runOn(Backend::CPU);
  const CoverageResult gpu = runOn(Backend::CUDA);
  ASSERT_EQ(cpu.powerDbm.size(), gpu.powerDbm.size());

  std::size_t both = 0, powerExact = 0, signalFlip = 0;
  for (std::size_t i = 0; i < cpu.powerDbm.size(); ++i) {
    const bool cs = std::isfinite(cpu.powerDbm[i]);
    const bool gs = std::isfinite(gpu.powerDbm[i]);
    if (cs != gs) {
      ++signalFlip;  // borderline shadow-edge cell: occlusion flipped
    } else if (cs) {
      ++both;
      if (cpu.powerDbm[i] == gpu.powerDbm[i]) ++powerExact;
    }
  }
  EXPECT_GT(both, 0u) << "no covered cells to compare";
  // Where both backends see the cell, LOS power is pure host-double FSPL -> exact.
  EXPECT_EQ(both, powerExact) << "power differs where both have signal";
  // Only cells grazing the shadow boundary may flip; require few (< 5%).
  EXPECT_LT(signalFlip, cpu.powerDbm.size() / 20)
      << signalFlip << " signal flips of " << cpu.powerDbm.size() << " cells";
}

#endif  // RFTRACE_HAVE_CUDA
