#include <gtest/gtest.h>

#include "rftrace/geometry.hpp"

using namespace rftrace;

TEST(Geometry, RayHitsTriangleInterior) {
  Triangle tri{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  Ray ray{{0.25, 0.25, 1.0}, {0, 0, -1}};
  const Hit h = intersectTriangle(ray, tri, 7);
  ASSERT_TRUE(h.valid);
  EXPECT_NEAR(h.t, 1.0, 1e-9);
  EXPECT_EQ(h.triangle, 7);
  EXPECT_GE(h.u, 0.0);
  EXPECT_GE(h.v, 0.0);
  EXPECT_LE(h.u + h.v, 1.0);
}

TEST(Geometry, RayMissesTriangle) {
  Triangle tri{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  Ray ray{{2.0, 2.0, 1.0}, {0, 0, -1}};
  EXPECT_FALSE(intersectTriangle(ray, tri).valid);
}

TEST(Geometry, ParallelRayIsStable) {
  Triangle tri{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  Ray ray{{0.25, 0.25, 1.0}, {1, 0, 0}};  // parallel to the z=0 plane
  const Hit h = intersectTriangle(ray, tri);
  EXPECT_FALSE(h.valid);
}

TEST(Geometry, DegenerateTriangleMisses) {
  Triangle tri{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  Ray ray{{0, 0, 1}, {0, 0, -1}};
  EXPECT_FALSE(intersectTriangle(ray, tri).valid);
}

TEST(Geometry, RespectsTInterval) {
  Triangle tri{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  Ray ray{{0.25, 0.25, 1.0}, {0, 0, -1}, 1e-4, 0.5};  // triangle at t=1 > tMax
  EXPECT_FALSE(intersectTriangle(ray, tri).valid);
}

TEST(Geometry, AabbSlabTest) {
  AABB box;
  box.expand(Vec3{0, 0, 0});
  box.expand(Vec3{1, 1, 1});
  double tNear = 0.0;
  Ray hit{{0.5, 0.5, -1}, {0, 0, 1}};
  EXPECT_TRUE(box.intersect(hit, tNear));
  Ray miss{{5, 5, -1}, {0, 0, 1}};
  EXPECT_FALSE(box.intersect(miss, tNear));
}
