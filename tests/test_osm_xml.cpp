#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "rftrace/importers/osm_importer.hpp"
#include "rftrace/scene.hpp"

using namespace rftrace;
namespace fs = std::filesystem;

namespace {
std::string writeTemp(const std::string& name, const std::string& content) {
  const fs::path p = fs::path(testing::TempDir()) / name;
  std::ofstream out(p);
  out << content;
  out.close();
  return p.string();
}

double maxZ(const Scene& s) {
  double m = -1e30;
  for (const Triangle& t : s.triangles())
    for (const Vec3& v : {t.v0, t.v1, t.v2}) m = std::max(m, v.z());
  return m;
}

bool hasTriangleWithMaterial(const Scene& s, const std::string& name) {
  for (std::size_t i = 0; i < s.triangles().size(); ++i)
    if (s.materialForTriangle(static_cast<int>(i)).name == name) return true;
  return false;
}

bool hasVertexNear(const Scene& s, const Vec3& p, double tol = 1e-3) {
  for (const Triangle& t : s.triangles())
    for (const Vec3& v : {t.v0, t.v1, t.v2})
      if ((v - p).norm() < tol) return true;
  return false;
}

// A small .osm XML fixture: one building way (height 12) and one wood way,
// plus the eight nodes they reference. Two square footprints of 0.001deg.
constexpr const char* kOsmXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6" generator="test">
  <bounds minlat="0" minlon="0" maxlat="0.003" maxlon="0.001"/>
  <node id="1" lat="0.0" lon="0.0"/>
  <node id="2" lat="0.0" lon="0.001"/>
  <node id="3" lat="0.001" lon="0.001"/>
  <node id="4" lat="0.001" lon="0.0"/>
  <node id="5" lat="0.002" lon="0.0"/>
  <node id="6" lat="0.002" lon="0.001"/>
  <node id="7" lat="0.003" lon="0.001"/>
  <node id="8" lat="0.003" lon="0.0"/>
  <way id="100">
    <nd ref="1"/>
    <nd ref="2"/>
    <nd ref="3"/>
    <nd ref="4"/>
    <nd ref="1"/>
    <tag k="building" v="yes"/>
    <tag k="height" v="12"/>
  </way>
  <way id="101">
    <nd ref="5"/>
    <nd ref="6"/>
    <nd ref="7"/>
    <nd ref="8"/>
    <nd ref="5"/>
    <tag k="landuse" v="forest"/>
  </way>
</osm>)";
}  // namespace

TEST(OsmXmlImport, BuildingAndVegetationExtruded) {
  const std::string path = writeTemp("scene.osm", kOsmXml);

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);  // deterministic projection
  const std::size_t tris = scene.loadOSMXml(path);

  // Two footprints -> 2 * 10 = 20 triangles.
  EXPECT_EQ(tris, 20u);
  EXPECT_EQ(scene.triangles().size(), 20u);

  // Building tagged height 12; vegetation default 8 m -> overall max is 12.
  EXPECT_NEAR(maxZ(scene), 12.0, 1e-6);

  EXPECT_TRUE(hasTriangleWithMaterial(scene, "concrete"));
  EXPECT_TRUE(hasTriangleWithMaterial(scene, "vegetation"));
}

TEST(OsmXmlImport, NodeReferencesResolveToProjectedPositions) {
  const std::string path = writeTemp("scene_proj.osm", kOsmXml);

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);
  scene.loadOSMXml(path);

  // Equirectangular projection about (0,0): x = lon*111320*cos(0),
  // y = lat*110540. The building corner node id=3 (lat=0.001, lon=0.001) at its
  // extruded top (z=12) must be present, confirming refs resolved + projected.
  const Vec3 corner = scene.geoProject(0.001, 0.001, 0.0);
  EXPECT_NEAR(corner.x(), 0.001 * 111320.0, 1e-3);
  EXPECT_NEAR(corner.y(), 0.001 * 110540.0, 1e-3);
  EXPECT_TRUE(hasVertexNear(scene, Vec3(corner.x(), corner.y(), 12.0)));
}

TEST(OsmXmlImport, ForestAreaBecomesVegetation) {
  // Only a forest way -> vegetation-only import at the default 8 m height.
  const std::string osm = R"(<osm version="0.6">
    <node id="5" lat="0.002" lon="0.0"/>
    <node id="6" lat="0.002" lon="0.001"/>
    <node id="7" lat="0.003" lon="0.001"/>
    <node id="8" lat="0.003" lon="0.0"/>
    <way id="101">
      <nd ref="5"/><nd ref="6"/><nd ref="7"/><nd ref="8"/><nd ref="5"/>
      <tag k="landuse" v="forest"/>
    </way>
  </osm>)";
  const std::string path = writeTemp("forest.osm", osm);

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);
  const std::size_t tris = scene.loadOSMXml(path);

  EXPECT_EQ(tris, 10u);
  EXPECT_NEAR(maxZ(scene), 8.0, 1e-6);
  EXPECT_TRUE(hasTriangleWithMaterial(scene, "vegetation"));
  EXPECT_FALSE(hasTriangleWithMaterial(scene, "concrete"));
}

TEST(OsmXmlImport, AutodetectRoutesXmlThroughLoadOSM) {
  const std::string path = writeTemp("auto.osm", kOsmXml);

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);
  // loadOSM must sniff the leading '<' and route to the XML reader.
  const std::size_t tris = scene.loadOSM(path);
  EXPECT_EQ(tris, 20u);
  EXPECT_TRUE(hasTriangleWithMaterial(scene, "concrete"));
  EXPECT_TRUE(hasTriangleWithMaterial(scene, "vegetation"));
}

TEST(OsmXmlImport, AutodetectStillParsesOverpassJson) {
  const std::string osm = R"({
    "version":0.6,"elements":[
      {"type":"node","id":1,"lat":0.0,"lon":0.0},
      {"type":"node","id":2,"lat":0.0,"lon":0.001},
      {"type":"node","id":3,"lat":0.001,"lon":0.001},
      {"type":"node","id":4,"lat":0.001,"lon":0.0},
      {"type":"way","id":100,"nodes":[1,2,3,4,1],
       "tags":{"building":"yes","height":"12"}}
    ]})";
  const std::string path = writeTemp("auto.json", osm);

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);
  const std::size_t tris = scene.loadOSM(path);
  EXPECT_EQ(tris, 10u);
  EXPECT_NEAR(maxZ(scene), 12.0, 1e-6);
  EXPECT_TRUE(hasTriangleWithMaterial(scene, "concrete"));
}

TEST(OsmXmlImport, MalformedXmlThrows) {
  // An unterminated tag (no closing '>') is structural corruption.
  const std::string path = writeTemp("bad.osm", "<osm><node id=\"1\" lat=");
  Scene scene;
  EXPECT_THROW(scene.loadOSMXml(path), SceneError);
  EXPECT_EQ(scene.triangles().size(), 0u);
}

TEST(OsmXmlImport, XmlWithNoBuildingOrVegetationThrows) {
  const std::string osm = R"(<osm version="0.6">
    <node id="1" lat="0.0" lon="0.0"/>
    <node id="2" lat="0.0" lon="0.001"/>
    <node id="3" lat="0.001" lon="0.001"/>
    <way id="100">
      <nd ref="1"/><nd ref="2"/><nd ref="3"/><nd ref="1"/>
      <tag k="highway" v="residential"/>
    </way>
  </osm>)";
  const std::string path = writeTemp("nogeom.osm", osm);
  Scene scene;
  EXPECT_THROW(scene.loadOSMXml(path), SceneError);
  EXPECT_EQ(scene.triangles().size(), 0u);
}
