// CPU-vs-Metal parity test suite. Built only when RFTRACE_HAVE_METAL is defined
// (RFTRACE_ENABLE_METAL=ON on Apple); every test skips at runtime when no Metal
// device is present, so the default CI is unaffected.
//
// The GPU acceleration structure and kernel work in float32 while the CPU
// reference (the BVH) works in double. Parity therefore compares hit/miss and
// triangle index exactly for well-separated geometry, and t within a tolerance
// (D4). Where float32 rounding legitimately flips a result — a ray grazing a
// triangle edge, sitting on the [tMin,tMax] boundary, or a near-tie between two
// triangles at almost the same depth — the disagreement is classified as
// "borderline" (verified against the exact double-precision geometry) rather
// than counted as a failure. Genuine traversal bugs produce large, non-
// borderline discrepancies and are asserted to be zero.
#if RFTRACE_HAVE_METAL

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "rftrace/backend.hpp"
#include "rftrace/geometry.hpp"
#include "rftrace/math.hpp"

using namespace rftrace;

namespace {

// ---- Tolerances -------------------------------------------------------------
// Absolute t tolerance in metres (D4) combined with a relative term so it stays
// meaningful for far hits in the ~100 m scenes below.
constexpr double kAbsT = 1e-2;
constexpr double kRelT = 1e-4;
// Barycentric distance to an edge below which a hit is "grazing" (float32 may
// flip inside/outside). Dimensionless.
constexpr double kEdgeEps = 2e-3;
// Distance (m) from a t-interval endpoint below which a hit is a boundary case.
constexpr double kBoundaryEps = 2e-2;
// Depth gap (m) between the two nearest triangles below which the winner is a
// near-tie and the exact triangle index may differ between float and double.
constexpr double kTieTol = 5e-2;

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

// A few hundred small, randomly-placed triangles in a [10,90]^3 box.
std::vector<Triangle> makeRandomTriangles(int n, unsigned seed) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<double> center(15.0, 85.0);
  std::uniform_real_distribution<double> off(-3.0, 3.0);
  std::vector<Triangle> tris;
  tris.reserve(n);
  for (int i = 0; i < n; ++i) {
    const Vec3 c(center(gen), center(gen), center(gen));
    // Return Vec3 (not the deduced Eigen expression type) so no dangling
    // temporary is referenced after the lambda returns.
    auto vert = [&]() -> Vec3 {
      return c + Vec3(off(gen), off(gen), off(gen));
    };
    tris.push_back({vert(), vert(), vert()});
  }
  return tris;
}

// ---- Ray builders (deterministic) ------------------------------------------

// Sampling box for ray endpoints: the scene's AABB padded so rays reliably
// traverse the geometry regardless of where it sits (a flat wall has zero
// extent on one axis, hence the additive margin).
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

// Rays between two random points in `b`, giving a high hit rate while staying
// reproducible for a fixed seed.
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

// Finite segment rays between two random points in `b` (for occlusion parity).
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
  for (std::size_t i = 0; i < n; ++i) rays.push_back(segmentRay(point(), point()));
  return rays;
}

// ---- Exact-geometry analysis (double precision reference) -------------------

struct Analysis {
  Hit hit;                 // brute-force closest hit
  bool nearTie = false;    // second-nearest triangle within kTieTol in t
  double edgeDist = 1.0;   // barycentric distance of the hit to nearest edge
  double boundaryGap = 1e9;  // distance of t to the nearest interval endpoint
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

// True if some triangle intersects the ray right at an edge or interval endpoint
// (within the epsilons). Such rays may legitimately flip hit/miss under float32.
bool hasBorderlineIntersection(const std::vector<Triangle>& tris,
                               const Ray& ray) {
  for (const Triangle& tri : tris) {
    // Möller–Trumbore without the range/uv rejection, to inspect the raw hit.
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
    if (ed < -kEdgeEps) continue;                       // clearly outside tri
    if (t < ray.tMin - kBoundaryEps) continue;          // clearly before
    if (t > ray.tMax + kBoundaryEps) continue;          // clearly after
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
  std::size_t strictIndexOk = 0;   // index matched with unambiguous depth
  std::size_t indexMismatch = 0;   // hard: differing index, not a near-tie
  std::size_t tMismatch = 0;       // both hit but t beyond tolerance
  std::size_t validHardMismatch = 0;   // hit/miss differs, not borderline
  std::size_t validSoftMismatch = 0;   // hit/miss differs, borderline (ok)
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

// Run the full closest-hit + occlusion parity comparison for one scene.
ParityStats runScene(const std::vector<Triangle>& tris, unsigned seed,
                     std::size_t nClosest, std::size_t nOccluded) {
  auto cpu = makeBackend(Backend::CPU, false);
  auto gpu = makeBackend(Backend::Metal, false);
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

// Assert the invariants that must hold for every scene, and require the scene
// to have exercised a meaningful number of strictly-matched hits.
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

bool metalReady() { return backendAvailable(Backend::Metal); }

}  // namespace

TEST(MetalParityScenes, EmptyScene) {
  if (!metalReady()) GTEST_SKIP() << "no Metal device available";
  auto cpu = makeBackend(Backend::CPU, false);
  auto gpu = makeBackend(Backend::Metal, false);
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

TEST(MetalParityScenes, SingleWall) {
  if (!metalReady()) GTEST_SKIP() << "no Metal device available";
  const auto s = runScene(makeWall(), 100, kClosestRays, kOccRays);
  expectParity(s);
}

TEST(MetalParityScenes, TwoBuildingCanyon) {
  if (!metalReady()) GTEST_SKIP() << "no Metal device available";
  const auto s = runScene(makeCanyon(), 200, kClosestRays, kOccRays);
  expectParity(s);
}

TEST(MetalParityScenes, HundredsOfRandomTriangles) {
  if (!metalReady()) GTEST_SKIP() << "no Metal device available";
  const auto tris = makeRandomTriangles(300, 42);
  const auto s = runScene(tris, 300, kClosestRays, kOccRays);
  expectParity(s);
  // Soft (borderline) mismatches are float32 edge/boundary/tie effects; they
  // should stay a small fraction of the rays even in the dense random scene.
  EXPECT_LT(s.validSoftMismatch + s.occSoftMismatch, kClosestRays / 20)
      << "unexpectedly many borderline disagreements";
}

// The Metal dispatch must be deterministic: the same rays twice yield identical
// results (bit-for-bit on the reported fields).
TEST(MetalParityScenes, DeterministicAcrossRuns) {
  if (!metalReady()) GTEST_SKIP() << "no Metal device available";
  auto gpu = makeBackend(Backend::Metal, false);
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

#endif  // RFTRACE_HAVE_METAL
