#include <gtest/gtest.h>

#include <cmath>

#include "rftrace/geo/footprint.hpp"
#include "rftrace/scene.hpp"

using namespace rftrace;

namespace {
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
}

TEST(GeoReference, ProjectRequiresOrigin) {
  Scene scene;
  EXPECT_FALSE(scene.hasGeoOrigin());
  EXPECT_THROW(scene.geoProject(13.1, -59.6), SceneError);
}

TEST(GeoReference, SetOriginMarksGeoreferenced) {
  Scene scene;
  scene.setGeoOrigin(13.0959, -59.6145);
  EXPECT_TRUE(scene.hasGeoOrigin());
  EXPECT_TRUE(scene.coordinateSystem().georeferenced);
  EXPECT_DOUBLE_EQ(scene.coordinateSystem().originLat, 13.0959);
  EXPECT_DOUBLE_EQ(scene.coordinateSystem().originLon, -59.6145);
}

TEST(GeoReference, OriginProjectsToLocalZero) {
  Scene scene;
  scene.setGeoOrigin(13.0959, -59.6145);
  const Vec3 p = scene.geoProject(13.0959, -59.6145, 42.0);
  EXPECT_NEAR(p.x(), 0.0, 1e-9);
  EXPECT_NEAR(p.y(), 0.0, 1e-9);
  EXPECT_DOUBLE_EQ(p.z(), 42.0);
}

TEST(GeoReference, KnownOffsetMapsToExpectedMeters) {
  const double lat0 = 13.0959;
  const double lon0 = -59.6145;
  Scene scene;
  scene.setGeoOrigin(lat0, lon0);

  // +0.001 deg lon east, +0.002 deg lat north.
  const Vec3 p = scene.geoProject(lat0 + 0.002, lon0 + 0.001, 0.0);
  const double expX = 0.001 * 111320.0 * std::cos(lat0 * kDegToRad);
  const double expY = 0.002 * 110540.0;
  EXPECT_NEAR(p.x(), expX, 1e-6);
  EXPECT_NEAR(p.y(), expY, 1e-6);
  EXPECT_GT(p.x(), 0.0);  // east is +x
  EXPECT_GT(p.y(), 0.0);  // north is +y
}

TEST(ExtrudeFootprint, SquareTriangleCountAndBounds) {
  // Unit square footprint, CCW, at base 5 m, height 10 m.
  std::vector<Vec3> ring = {
      {0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}};
  const auto tris = geo::extrudeFootprint(ring, 5.0, 10.0);

  // 4 walls * 2 + (4 - 2) roof = 10 triangles.
  ASSERT_EQ(tris.size(), 10u);

  double minZ = 1e18, maxZ = -1e18, minX = 1e18, maxX = -1e18;
  for (const auto& t : tris) {
    for (const Vec3& v : {t.v0, t.v1, t.v2}) {
      minZ = std::min(minZ, v.z());
      maxZ = std::max(maxZ, v.z());
      minX = std::min(minX, v.x());
      maxX = std::max(maxX, v.x());
    }
  }
  EXPECT_DOUBLE_EQ(minZ, 5.0);
  EXPECT_DOUBLE_EQ(maxZ, 15.0);
  EXPECT_DOUBLE_EQ(minX, 0.0);
  EXPECT_DOUBLE_EQ(maxX, 2.0);
}

TEST(ExtrudeFootprint, ClosedRingIsDeduplicated) {
  // Same square but with an explicit closing vertex; must match the open ring.
  std::vector<Vec3> closed = {
      {0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}, {0, 0, 0}};
  const auto tris = geo::extrudeFootprint(closed, 0.0, 3.0);
  EXPECT_EQ(tris.size(), 10u);
}

TEST(ExtrudeFootprint, DegenerateRingYieldsNoTriangles) {
  std::vector<Vec3> line = {{0, 0, 0}, {1, 1, 0}};
  EXPECT_TRUE(geo::extrudeFootprint(line, 0.0, 5.0).empty());
}

TEST(ExtrudeFootprint, RoofTrianglesAreHorizontalAtTop) {
  std::vector<Vec3> ring = {{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}};
  const auto tris = geo::extrudeFootprint(ring, 0.0, 8.0);
  // Last (n-2)=2 triangles are the roof: all vertices at z = height, normal +/-z.
  for (std::size_t i = tris.size() - 2; i < tris.size(); ++i) {
    EXPECT_DOUBLE_EQ(tris[i].v0.z(), 8.0);
    EXPECT_DOUBLE_EQ(tris[i].v1.z(), 8.0);
    EXPECT_DOUBLE_EQ(tris[i].v2.z(), 8.0);
    const Vec3 n = tris[i].normal();
    EXPECT_NEAR(std::abs(n.z()), 1.0, 1e-9);
  }
}
