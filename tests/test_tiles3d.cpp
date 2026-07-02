#include <gtest/gtest.h>

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>

#include <assimp/Importer.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "rftrace/exporters/tiles3d_exporter.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
RFResult reflectionResult() {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  Transmitter tx;
  tx.id = "tx";
  tx.position = {100, 20, 20};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);
  Receiver rx;
  rx.id = "rx";
  rx.position = {200, 20, 10};
  scene.addReceiver(rx);
  SimulationSettings s;
  s.maxReflections = 1;
  return Simulator(s).run(scene);
}
}  // namespace

TEST(Tiles3D, TilesetIsValidAndReferencesGlb) {
  const RFResult r = reflectionResult();
  const fs::path dir = fs::path(testing::TempDir()) / "rftrace_3dtiles";
  fs::remove_all(dir);
  io::exportPaths3DTiles(r, dir.string(), /*includeReceivers=*/true);

  const fs::path tilesetPath = dir / "tileset.json";
  ASSERT_TRUE(fs::exists(tilesetPath));

  std::ifstream in(tilesetPath);
  std::stringstream ss;
  ss << in.rdbuf();
  const json ts = json::parse(ss.str());

  // 3D Tiles 1.1 asset + numeric root geometricError.
  EXPECT_EQ(ts.at("asset").at("version").get<std::string>(), "1.1");
  EXPECT_TRUE(ts.at("geometricError").is_number());

  const json& root = ts.at("root");
  EXPECT_TRUE(root.at("boundingVolume").contains("box"));
  EXPECT_EQ(root.at("boundingVolume").at("box").size(), 12u);
  EXPECT_TRUE(root.at("geometricError").is_number());

  // Root tile references a content file that was actually written.
  const std::string uri = root.at("content").at("uri").get<std::string>();
  const fs::path contentPath = dir / uri;
  ASSERT_TRUE(fs::exists(contentPath));
  EXPECT_EQ(contentPath.extension(), ".glb");
}

TEST(Tiles3D, ContentGlbReimportsViaAssimp) {
  const RFResult r = reflectionResult();
  const fs::path dir = fs::path(testing::TempDir()) / "rftrace_3dtiles_glb";
  fs::remove_all(dir);
  io::exportPaths3DTiles(r, dir.string(), /*includeReceivers=*/true);

  const fs::path glb = dir / "content.glb";
  ASSERT_TRUE(fs::exists(glb));

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(glb.string(), 0);
  ASSERT_NE(scene, nullptr) << importer.GetErrorString();
  ASSERT_GT(scene->mNumMeshes, 0u);

  unsigned totalVerts = 0;
  for (unsigned m = 0; m < scene->mNumMeshes; ++m)
    totalVerts += scene->mMeshes[m]->mNumVertices;
  EXPECT_GT(totalVerts, 0u);
}
