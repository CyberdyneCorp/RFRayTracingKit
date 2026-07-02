#include <gtest/gtest.h>

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <filesystem>

#include "rftrace/exporters/gltf_exporter.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;
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

TEST(Gltf, ExportReimportsViaAssimp) {
  const RFResult r = reflectionResult();
  const fs::path out = fs::path(testing::TempDir()) / "paths.gltf";
  io::exportPathsGltf(r, out.string(), /*includeReceivers=*/true);

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(out.string(), 0);
  ASSERT_NE(scene, nullptr) << importer.GetErrorString();
  ASSERT_GT(scene->mNumMeshes, 0u);

  unsigned totalVerts = 0;
  for (unsigned m = 0; m < scene->mNumMeshes; ++m)
    totalVerts += scene->mMeshes[m]->mNumVertices;
  EXPECT_GT(totalVerts, 0u);
}

TEST(Gltf, StringIsValidGltfJson) {
  const std::string g = io::pathsToGltfString(reflectionResult());
  EXPECT_NE(g.find("\"asset\""), std::string::npos);
  EXPECT_NE(g.find("\"2.0\""), std::string::npos);
  EXPECT_NE(g.find("data:application/octet-stream;base64,"),
            std::string::npos);
}
