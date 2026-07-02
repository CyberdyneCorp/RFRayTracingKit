#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

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

// A canonical minimal glTF 2.0 single triangle with an embedded base64 buffer.
// Positions: (0,0,0),(1,0,0),(0,1,0); indices 0,1,2.
const char* kMinimalGltf = R"({
  "scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
  "meshes":[{"primitives":[{"attributes":{"POSITION":1},"indices":0}]}],
  "buffers":[{"uri":"data:application/octet-stream;base64,AAABAAIAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAA=","byteLength":44}],
  "bufferViews":[
    {"buffer":0,"byteOffset":0,"byteLength":6,"target":34963},
    {"buffer":0,"byteOffset":8,"byteLength":36,"target":34962}
  ],
  "accessors":[
    {"bufferView":0,"byteOffset":0,"componentType":5123,"count":3,"type":"SCALAR","max":[2],"min":[0]},
    {"bufferView":1,"byteOffset":0,"componentType":5126,"count":3,"type":"VEC3","max":[1.0,1.0,0.0],"min":[0.0,0.0,0.0]}
  ],
  "asset":{"version":"2.0"}
})";
}  // namespace

TEST(Import, ObjQuadIsTriangulated) {
  // A single quad face -> two triangles after import.
  const std::string obj =
      "v 0 0 0\nv 1 0 0\nv 1 0 1\nv 0 0 1\nf 1 2 3 4\n";
  const std::string path = writeTemp("quad.obj", obj);

  Scene scene;
  const std::size_t n = scene.loadMesh(path);
  EXPECT_EQ(n, 2u);
  EXPECT_EQ(scene.triangles().size(), 2u);
}

TEST(Import, ObjYUpConvertedToZUp) {
  // Vertex with height in Y should have that height in Z after import.
  const std::string obj = "v 0 1 0\nv 1 1 0\nv 0 1 0.5\nf 1 2 3\n";
  const std::string path = writeTemp("yup.obj", obj);

  Scene scene;
  ASSERT_EQ(scene.loadMesh(path), 1u);
  const Triangle& t = scene.triangles().front();
  // All three vertices had y=1 -> z≈1 after Y-up→Z-up.
  EXPECT_NEAR(t.v0.z(), 1.0, 1e-6);
  EXPECT_NEAR(t.v1.z(), 1.0, 1e-6);
}

TEST(Import, GltfTriangleLoads) {
  const std::string path = writeTemp("tri.gltf", kMinimalGltf);
  Scene scene;
  const std::size_t n = scene.loadMesh(path);
  EXPECT_EQ(n, 1u);

  // The (0,1,0) Y-up vertex becomes z≈1 in the Z-up scene.
  double maxZ = -1e30;
  const Triangle& t = scene.triangles().front();
  for (const Vec3& v : {t.v0, t.v1, t.v2}) maxZ = std::max(maxZ, v.z());
  EXPECT_NEAR(maxZ, 1.0, 1e-5);
}

TEST(Import, MissingFileThrows) {
  Scene scene;
  EXPECT_THROW(scene.loadMesh("/no/such/file.obj"), SceneError);
}

TEST(Import, UnknownMaterialOnLoadThrows) {
  const std::string path = writeTemp("m.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  Scene scene;
  EXPECT_THROW(scene.loadMesh(path, "not-a-material"), SceneError);
}

TEST(Import, MaterialsJsonLoads) {
  const std::string json = R"({"materials":[
    {"name":"glass","relativePermittivity":6.27,"conductivity":0.0043,
     "penetrationLossDb":8.0,"reflectionLossDb":3.0},
    {"name":"steel","reflection_loss_db":0.5,"penetration_loss_db":100.0}
  ]})";
  const std::string path = writeTemp("mats.json", json);

  Scene scene;
  const std::size_t n = scene.loadMaterials(path);
  EXPECT_EQ(n, 2u);
  ASSERT_TRUE(scene.materialIndex("glass").has_value());
  EXPECT_NEAR(scene.material(*scene.materialIndex("glass")).relativePermittivity,
              6.27, 1e-9);
  EXPECT_NEAR(scene.material(*scene.materialIndex("steel")).reflectionLossDb,
              0.5, 1e-9);
}

TEST(Import, MalformedMaterialsJsonThrows) {
  const std::string path = writeTemp("bad.json", "{ this is not json ");
  Scene scene;
  EXPECT_THROW(scene.loadMaterials(path), SceneError);
}

TEST(Import, MaterialEntryMissingNameThrows) {
  const std::string path = writeTemp("noname.json", R"([{"conductivity":0.1}])");
  Scene scene;
  EXPECT_THROW(scene.loadMaterials(path), SceneError);
}
