#include <gtest/gtest.h>

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>

#include <assimp/Importer.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

#include "rftrace/exporters/tiles3d_exporter.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
// A scene with receivers spread across the horizontal (X/Y) plane so the
// quadtree populates more than one quadrant.
RFResult spreadResult() {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  const Vec3 a{-200, 400, 0}, b{600, 400, 0}, c{600, 400, 60}, d{-200, 400, 60};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  Transmitter tx;
  tx.id = "tx";
  tx.position = {200, 50, 25};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);

  // Four receivers, one per horizontal quadrant of the X/Y extent.
  const Vec3 positions[] = {
      {0, 0, 10}, {400, 0, 10}, {0, 200, 10}, {400, 200, 10}};
  int i = 0;
  for (const Vec3& p : positions) {
    Receiver rx;
    rx.id = "rx" + std::to_string(i++);
    rx.position = p;
    scene.addReceiver(rx);
  }

  SimulationSettings s;
  s.maxReflections = 1;
  return Simulator(s).run(scene);
}

// Re-import a tile's content .glb via Assimp and assert it has vertices.
void checkContentGlb(const fs::path& glb) {
  EXPECT_EQ(glb.extension(), ".glb");
  ASSERT_TRUE(fs::exists(glb)) << glb.string();
  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(glb.string(), 0);
  ASSERT_NE(scene, nullptr) << importer.GetErrorString();
  ASSERT_GT(scene->mNumMeshes, 0u);
  unsigned verts = 0;
  for (unsigned m = 0; m < scene->mNumMeshes; ++m)
    verts += scene->mMeshes[m]->mNumVertices;
  EXPECT_GT(verts, 0u);
}

// Recursively verify every tile: box bounding volume, content .glb re-imports
// via Assimp with vertices, and each child's geometricError is strictly less
// than its parent's. Records the deepest level reached into `maxDepth`.
void walkTile(const json& tile, const fs::path& dir, double parentError,
              int depth, int& maxDepth) {
  maxDepth = std::max(maxDepth, depth);
  EXPECT_TRUE(tile.at("boundingVolume").contains("box"));
  EXPECT_EQ(tile.at("boundingVolume").at("box").size(), 12u);
  ASSERT_TRUE(tile.at("geometricError").is_number());
  const double error = tile.at("geometricError").get<double>();
  EXPECT_LT(error, parentError) << "geometricError must decrease with depth";

  if (tile.contains("content"))
    checkContentGlb(dir / tile.at("content").at("uri").get<std::string>());

  if (tile.contains("children"))
    for (const json& child : tile.at("children"))
      walkTile(child, dir, error, depth + 1, maxDepth);
}
}  // namespace

TEST(Tiles3DLod, HierarchicalTilesetIsValid) {
  const RFResult r = spreadResult();
  const fs::path dir = fs::path(testing::TempDir()) / "rftrace_3dtiles_lod";
  fs::remove_all(dir);
  io::exportPaths3DTilesLod(r, dir.string(), /*maxDepth=*/2,
                            /*includeReceivers=*/true);

  const fs::path tilesetPath = dir / "tileset.json";
  ASSERT_TRUE(fs::exists(tilesetPath));
  std::ifstream in(tilesetPath);
  std::stringstream ss;
  ss << in.rdbuf();
  const json ts = json::parse(ss.str());

  EXPECT_EQ(ts.at("asset").at("version").get<std::string>(), "1.1");
  ASSERT_TRUE(ts.at("geometricError").is_number());
  const double rootTilesetError = ts.at("geometricError").get<double>();

  const json& root = ts.at("root");
  // Root must refine into children (hierarchy present, depth >= 1).
  ASSERT_TRUE(root.contains("children"));
  ASSERT_FALSE(root.at("children").empty());
  EXPECT_EQ(root.at("refine").get<std::string>(), "REPLACE");

  // Walk the whole tree: valid boxes, re-importable .glb content, and
  // geometricError strictly decreasing from parent to child.
  int maxDepth = 0;
  walkTile(root, dir, rootTilesetError + 1.0, /*depth=*/0, maxDepth);
  EXPECT_GE(maxDepth, 1) << "expected at least one level of child tiles";
}

TEST(Tiles3DLod, MaxDepthIsClampedToAtLeastOne) {
  const RFResult r = spreadResult();
  const fs::path dir = fs::path(testing::TempDir()) / "rftrace_3dtiles_lod0";
  fs::remove_all(dir);
  // maxDepth <= 0 is clamped to 1, so a hierarchy is still produced.
  io::exportPaths3DTilesLod(r, dir.string(), /*maxDepth=*/0,
                            /*includeReceivers=*/true);

  std::ifstream in(dir / "tileset.json");
  std::stringstream ss;
  ss << in.rdbuf();
  const json ts = json::parse(ss.str());
  ASSERT_TRUE(ts.at("root").contains("children"));
  EXPECT_FALSE(ts.at("root").at("children").empty());
}
