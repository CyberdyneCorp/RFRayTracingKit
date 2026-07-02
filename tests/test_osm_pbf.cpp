#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "rftrace/importers/osm_pbf_importer.hpp"
#include "rftrace/scene.hpp"

using namespace rftrace;
namespace fs = std::filesystem;

// The OSM PBF reader is gated on RFTRACE_ENABLE_OSMIUM. When ON we author a tiny
// `.osm.pbf` with libosmium's writer and load it back, asserting the SAME
// building/vegetation extraction as the XML/Overpass paths. When OFF we assert
// graceful degradation: osmiumAvailable() is false and loadOSMPbf throws.

#if RFTRACE_HAVE_OSMIUM

#include <osmium/builder/attr.hpp>
#include <osmium/io/pbf_output.hpp>
#include <osmium/io/writer.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/location.hpp>

namespace {
bool hasTriangleWithMaterial(const Scene& s, const std::string& name) {
  for (std::size_t i = 0; i < s.triangles().size(); ++i)
    if (s.materialForTriangle(static_cast<int>(i)).name == name) return true;
  return false;
}

double maxZ(const Scene& s) {
  double m = -1e30;
  for (const Triangle& t : s.triangles())
    for (const Vec3& v : {t.v0, t.v1, t.v2}) m = std::max(m, v.z());
  return m;
}

// Write a minimal `.osm.pbf`: two square footprints (a building tagged
// height=12 and a landuse=forest area) sharing the geometry of the XML fixture,
// so extraction must yield 2 * 10 = 20 triangles.
std::string writeTinyPbf(const std::string& name) {
  using namespace osmium::builder::attr;
  using osmium::builder::add_node;
  using osmium::builder::add_way;
  osmium::memory::Buffer buffer{4096,
                                osmium::memory::Buffer::auto_grow::yes};
  // osmium::Location is constructed (lon, lat).
  add_node(buffer, _id(1), _location(osmium::Location{0.0, 0.0}));
  add_node(buffer, _id(2), _location(osmium::Location{0.001, 0.0}));
  add_node(buffer, _id(3), _location(osmium::Location{0.001, 0.001}));
  add_node(buffer, _id(4), _location(osmium::Location{0.0, 0.001}));
  add_node(buffer, _id(5), _location(osmium::Location{0.0, 0.002}));
  add_node(buffer, _id(6), _location(osmium::Location{0.001, 0.002}));
  add_node(buffer, _id(7), _location(osmium::Location{0.001, 0.003}));
  add_node(buffer, _id(8), _location(osmium::Location{0.0, 0.003}));
  add_way(buffer, _id(100), _nodes({1, 2, 3, 4, 1}),
          _tags({{"building", "yes"}, {"height", "12"}}));
  add_way(buffer, _id(101), _nodes({5, 6, 7, 8, 5}),
          _tags({{"landuse", "forest"}}));
  buffer.commit();

  const fs::path p = fs::path(testing::TempDir()) / name;
  osmium::io::Header header;
  header.set("generator", "rftrace-test");
  osmium::io::File outfile{p.string(), "pbf"};
  osmium::io::Writer writer{outfile, header, osmium::io::overwrite::allow};
  writer(std::move(buffer));
  writer.close();
  return p.string();
}
}  // namespace

TEST(OsmPbfImport, AvailableWhenBuiltWithOsmium) {
  EXPECT_TRUE(io::osmiumAvailable());
}

TEST(OsmPbfImport, BuildingAndVegetationExtruded) {
  const std::string path = writeTinyPbf("scene.osm.pbf");

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);  // deterministic projection
  const std::size_t tris = scene.loadOSMPbf(path);

  // Two footprints -> 2 * 10 = 20 triangles.
  EXPECT_EQ(tris, 20u);
  EXPECT_EQ(scene.triangles().size(), 20u);

  // Building tagged height 12; vegetation default 8 m -> overall max is 12.
  EXPECT_NEAR(maxZ(scene), 12.0, 1e-6);

  EXPECT_TRUE(hasTriangleWithMaterial(scene, "concrete"));
  EXPECT_TRUE(hasTriangleWithMaterial(scene, "vegetation"));
}

TEST(OsmPbfImport, NodeReferencesResolveToProjectedPositions) {
  const std::string path = writeTinyPbf("scene_proj.osm.pbf");

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);
  scene.loadOSMPbf(path);

  // Building corner node id=3 (lat=0.001, lon=0.001) at its extruded top (z=12)
  // must be present, confirming refs resolved + projected identically to the
  // XML/Overpass readers.
  const Vec3 corner = scene.geoProject(0.001, 0.001, 0.0);
  EXPECT_NEAR(corner.x(), 0.001 * 111320.0, 1e-3);
  EXPECT_NEAR(corner.y(), 0.001 * 110540.0, 1e-3);
  bool found = false;
  for (const Triangle& t : scene.triangles())
    for (const Vec3& v : {t.v0, t.v1, t.v2})
      if ((v - Vec3(corner.x(), corner.y(), 12.0)).norm() < 1e-3) found = true;
  EXPECT_TRUE(found);
}

TEST(OsmPbfImport, MissingFileThrows) {
  Scene scene;
  EXPECT_THROW(scene.loadOSMPbf("/nonexistent/path/to/file.osm.pbf"),
               SceneError);
  EXPECT_EQ(scene.triangles().size(), 0u);
}

#else  // !RFTRACE_HAVE_OSMIUM

TEST(OsmPbfImport, GracefulWithoutOsmium) {
  EXPECT_FALSE(io::osmiumAvailable());
  Scene scene;
  // The entry point is still declared; it must throw a clear runtime error.
  EXPECT_THROW(scene.loadOSMPbf("anything.osm.pbf"), std::exception);
  EXPECT_EQ(scene.triangles().size(), 0u);
}

#endif  // RFTRACE_HAVE_OSMIUM
