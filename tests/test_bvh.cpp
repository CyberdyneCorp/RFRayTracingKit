#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "rftrace/bvh.hpp"

using namespace rftrace;

namespace {
std::vector<Triangle> randomTriangles(int n, std::mt19937& rng) {
  std::uniform_real_distribution<double> pos(-10.0, 10.0);
  std::uniform_real_distribution<double> sz(0.1, 1.5);
  std::vector<Triangle> tris;
  tris.reserve(n);
  for (int i = 0; i < n; ++i) {
    const Vec3 o{pos(rng), pos(rng), pos(rng)};
    tris.push_back(Triangle{o, o + Vec3{sz(rng), 0, 0}, o + Vec3{0, sz(rng), sz(rng)}});
  }
  return tris;
}
}  // namespace

TEST(BVH, EmptyMeshMissesEverything) {
  BVH bvh;
  bvh.build({});
  EXPECT_EQ(bvh.triangleCount(), 0u);
  EXPECT_FALSE(bvh.closestHit(Ray{{0, 0, 0}, {0, 0, 1}}).valid);
  EXPECT_FALSE(bvh.occluded(Ray{{0, 0, 0}, {0, 0, 1}}));
}

TEST(BVH, ClosestHitMatchesBruteForce) {
  std::mt19937 rng(12345);
  const auto tris = randomTriangles(400, rng);
  BVH bvh;
  bvh.build(tris);

  std::uniform_real_distribution<double> pos(-12.0, 12.0);
  std::uniform_real_distribution<double> dir(-1.0, 1.0);
  int checked = 0;
  for (int i = 0; i < 2000; ++i) {
    Vec3 d{dir(rng), dir(rng), dir(rng)};
    if (d.norm() < 1e-6) continue;
    Ray ray{{pos(rng), pos(rng), pos(rng)}, d.normalized()};
    const Hit a = bvh.closestHit(ray);
    const Hit b = closestHitBruteForce(tris, ray);
    ASSERT_EQ(a.valid, b.valid) << "ray " << i;
    if (a.valid) {
      EXPECT_NEAR(a.t, b.t, 1e-6) << "ray " << i;
      ++checked;
    }
  }
  EXPECT_GT(checked, 0);  // ensure some rays actually hit
}

TEST(BVH, OcclusionBlockedAndClear) {
  // A single wall in the z=0 plane spanning [-5,5]^2.
  std::vector<Triangle> tris = {
      Triangle{{-5, -5, 0}, {5, -5, 0}, {5, 5, 0}},
      Triangle{{-5, -5, 0}, {5, 5, 0}, {-5, 5, 0}},
  };
  BVH bvh;
  bvh.build(tris);

  Ray blocked = segmentRay(Vec3{0, 0, -1}, Vec3{0, 0, 1});
  EXPECT_TRUE(bvh.occluded(blocked));

  Ray clear = segmentRay(Vec3{0, 0, 1}, Vec3{0, 0, 2});  // both above the wall
  EXPECT_FALSE(bvh.occluded(clear));
}

TEST(BVH, EndpointOnSurfaceDoesNotSelfOcclude) {
  std::vector<Triangle> tris = {
      Triangle{{-5, -5, 0}, {5, -5, 0}, {5, 5, 0}},
      Triangle{{-5, -5, 0}, {5, 5, 0}, {-5, 5, 0}},
  };
  BVH bvh;
  bvh.build(tris);
  // Segment starts on the wall (a reflection point) and leaves it: not occluded.
  Ray seg = segmentRay(Vec3{0, 0, 0}, Vec3{1, 1, 2});
  EXPECT_FALSE(bvh.occluded(seg));
}
