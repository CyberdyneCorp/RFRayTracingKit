#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "rftrace/importers/cityjson_importer.hpp"
#include "rftrace/importers/geojson_importer.hpp"
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

bool hasTriangleTopAt(const Scene& s, double z, double tol = 1e-6) {
  for (const Triangle& t : s.triangles())
    for (const Vec3& v : {t.v0, t.v1, t.v2})
      if (std::abs(v.z() - z) < tol) return true;
  return false;
}
}  // namespace

// --- GeoJSON -----------------------------------------------------------------

TEST(GeoJsonImport, PolygonBuildingAndPoint) {
  // A 0.001deg square with height=10 plus a receiver point at its centre.
  const std::string gj = R"({
    "type":"FeatureCollection","features":[
      {"type":"Feature","properties":{"height":10},
       "geometry":{"type":"Polygon","coordinates":[[[0,0],[0.001,0],[0.001,0.001],[0,0.001],[0,0]]]}},
      {"type":"Feature","properties":{"id":"rx1"},
       "geometry":{"type":"Point","coordinates":[0.0005,0.0005]}}
    ]})";
  const std::string path = writeTemp("buildings.geojson", gj);

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);  // deterministic projection
  const std::size_t tris = scene.loadGeoJSON(path);

  // 4-corner footprint -> 8 wall + 2 roof triangles.
  EXPECT_EQ(tris, 10u);
  EXPECT_EQ(scene.triangles().size(), 10u);
  EXPECT_NEAR(maxZ(scene), 10.0, 1e-6);

  ASSERT_EQ(scene.receivers().size(), 1u);
  const Receiver& rx = scene.receivers().front();
  EXPECT_EQ(rx.id, "rx1");
  EXPECT_NEAR(rx.position.x(), 0.0005 * 111320.0, 1e-3);
  EXPECT_NEAR(rx.position.y(), 0.0005 * 110540.0, 1e-3);
  EXPECT_NEAR(rx.position.z(), 0.0, 1e-9);
}

TEST(GeoJsonImport, MultiPolygonYieldsMultipleBuildings) {
  const std::string gj = R"({
    "type":"FeatureCollection","features":[
      {"type":"Feature","properties":{"levels":2},
       "geometry":{"type":"MultiPolygon","coordinates":[
         [[[0,0],[0.001,0],[0.001,0.001],[0,0.001],[0,0]]],
         [[[0.002,0],[0.003,0],[0.003,0.001],[0.002,0.001],[0.002,0]]]
       ]}}
    ]})";
  const std::string path = writeTemp("multi.geojson", gj);

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);
  const std::size_t tris = scene.loadGeoJSON(path);
  // Two footprints -> 20 triangles; levels=2 -> 6 m top.
  EXPECT_EQ(tris, 20u);
  EXPECT_NEAR(maxZ(scene), 2.0 * 3.0, 1e-6);
}

TEST(GeoJsonImport, PointAsTransmitterViaArgument) {
  const std::string gj = R"({
    "type":"FeatureCollection","features":[
      {"type":"Feature","properties":{},
       "geometry":{"type":"Point","coordinates":[0.001,0.002]}}
    ]})";
  const std::string path = writeTemp("tx.geojson", gj);

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);
  scene.loadGeoJSON(path, "concrete", "transmitter");
  ASSERT_EQ(scene.transmitters().size(), 1u);
  EXPECT_EQ(scene.receivers().size(), 0u);
}

TEST(GeoJsonImport, CentroidBecomesOriginWhenUnset) {
  const std::string gj = R"({
    "type":"FeatureCollection","features":[
      {"type":"Feature","properties":{"height":5},
       "geometry":{"type":"Polygon","coordinates":[
         [[20.0,10.0],[20.002,10.0],[20.002,10.002],[20.0,10.002],[20.0,10.0]]]}}
    ]})";
  const std::string path = writeTemp("centroid.geojson", gj);

  Scene scene;
  ASSERT_FALSE(scene.hasGeoOrigin());
  scene.loadGeoJSON(path);
  EXPECT_TRUE(scene.hasGeoOrigin());
  EXPECT_NEAR(scene.coordinateSystem().originLon, 20.0, 0.01);
  EXPECT_NEAR(scene.coordinateSystem().originLat, 10.0, 0.01);
}

TEST(GeoJsonImport, InvalidGeoJsonThrowsAndLeavesSceneUnchanged) {
  const std::string path = writeTemp("bad.geojson", "{ not json ");
  Scene scene;
  EXPECT_THROW(scene.loadGeoJSON(path), SceneError);
  EXPECT_EQ(scene.triangles().size(), 0u);

  const std::string noGeom =
      writeTemp("empty.geojson",
                R"({"type":"FeatureCollection","features":[]})");
  EXPECT_THROW(scene.loadGeoJSON(noGeom), SceneError);
  EXPECT_EQ(scene.triangles().size(), 0u);
  EXPECT_FALSE(scene.hasGeoOrigin());
}

// --- CityJSON ----------------------------------------------------------------

TEST(CityJsonImport, BuildingBoxWithTransform) {
  // Unit-ish cube as a MultiSurface. transform scales/translates the integer
  // vertices; the z transform (scale 2, translate 5) is verified after import.
  const std::string cj = R"({
    "type":"CityJSON","version":"1.1",
    "transform":{"scale":[1e-6,1e-6,2.0],"translate":[0,0,5.0]},
    "vertices":[
      [0,0,0],[1000,0,0],[1000,1000,0],[0,1000,0],
      [0,0,1],[1000,0,1],[1000,1000,1],[0,1000,1]],
    "CityObjects":{
      "b1":{"type":"Building","geometry":[
        {"type":"MultiSurface","lod":"2","boundaries":[
          [[0,1,2,3]],[[4,5,6,7]],
          [[0,1,5,4]],[[1,2,6,5]],[[2,3,7,6]],[[3,0,4,7]]
        ]}
      ]}
    }})";
  const std::string path = writeTemp("box.city.json", cj);

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);
  const std::size_t tris = scene.loadCityJSON(path);
  // 6 quad faces -> 12 triangles.
  EXPECT_EQ(tris, 12u);
  EXPECT_EQ(scene.triangles().size(), 12u);
  // z = iz*2 + 5 -> bottom 5, top 7 (transform applied, z passed through).
  EXPECT_NEAR(maxZ(scene), 7.0, 1e-9);
  EXPECT_TRUE(hasTriangleTopAt(scene, 5.0));
}

TEST(CityJsonImport, InvalidCityJsonThrows) {
  const std::string path =
      writeTemp("bad.city.json", R"({"type":"NotCityJSON"})");
  Scene scene;
  EXPECT_THROW(scene.loadCityJSON(path), SceneError);
  EXPECT_EQ(scene.triangles().size(), 0u);
}

// --- OSM (Overpass JSON) -----------------------------------------------------

TEST(OsmImport, BuildingsVegetationAndHeightHeuristics) {
  const std::string osm = R"({
    "version":0.6,"elements":[
      {"type":"node","id":1,"lat":0.0,"lon":0.0},
      {"type":"node","id":2,"lat":0.0,"lon":0.001},
      {"type":"node","id":3,"lat":0.001,"lon":0.001},
      {"type":"node","id":4,"lat":0.001,"lon":0.0},
      {"type":"node","id":5,"lat":0.002,"lon":0.0},
      {"type":"node","id":6,"lat":0.002,"lon":0.001},
      {"type":"node","id":7,"lat":0.003,"lon":0.001},
      {"type":"node","id":8,"lat":0.003,"lon":0.0},
      {"type":"node","id":9,"lat":0.004,"lon":0.0},
      {"type":"node","id":10,"lat":0.004,"lon":0.001},
      {"type":"node","id":11,"lat":0.005,"lon":0.001},
      {"type":"node","id":12,"lat":0.005,"lon":0.0},
      {"type":"way","id":100,"nodes":[1,2,3,4,1],
       "tags":{"building":"yes","height":"12"}},
      {"type":"way","id":101,"nodes":[5,6,7,8,5],
       "tags":{"natural":"wood"}},
      {"type":"way","id":102,"nodes":[9,10,11,12,9],
       "tags":{"building":"yes","building:levels":"3"}}
    ]})";
  const std::string path = writeTemp("osm.json", osm);

  Scene scene;
  scene.setGeoOrigin(0.0, 0.0);
  const std::size_t tris = scene.loadOSM(path);
  // Three footprints -> 3 * 10 = 30 triangles.
  EXPECT_EQ(tris, 30u);
  EXPECT_EQ(scene.triangles().size(), 30u);

  // Tagged height 12 (max), levels 3 -> 9 m, vegetation -> 8 m.
  EXPECT_NEAR(maxZ(scene), 12.0, 1e-6);
  EXPECT_TRUE(hasTriangleTopAt(scene, 9.0));
  EXPECT_TRUE(hasTriangleTopAt(scene, 8.0));

  EXPECT_TRUE(hasTriangleWithMaterial(scene, "concrete"));
  EXPECT_TRUE(hasTriangleWithMaterial(scene, "vegetation"));
}

TEST(OsmImport, InvalidOverpassJsonThrows) {
  const std::string path = writeTemp("bad_osm.json", R"({"foo":1})");
  Scene scene;
  EXPECT_THROW(scene.loadOSM(path), SceneError);
  EXPECT_EQ(scene.triangles().size(), 0u);
}
