#include "rftrace/importers/mesh_importer.hpp"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <vector>

namespace rftrace {

namespace {
/// Convert a right-handed Y-up vertex (glTF/OBJ convention) to the core's
/// right-handed Z-up frame: a +90° rotation about X maps Y (up) to Z.
inline Vec3 yUpToZUp(double x, double y, double z) { return Vec3(x, -z, y); }
}  // namespace

std::size_t Scene::loadMesh(const std::string& path,
                            const std::string& material) {
  int matIndex = -1;
  if (!material.empty()) {
    auto idx = materialIndex(material);
    if (!idx) throw SceneError("unknown material '" + material + "'");
    matIndex = *idx;
  }

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(
      path, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                aiProcess_PreTransformVertices);
  if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) ||
      scene->mRootNode == nullptr) {
    throw SceneError("failed to load mesh '" + path +
                     "': " + importer.GetErrorString());
  }

  std::vector<Triangle> triangles;
  for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
    const aiMesh* mesh = scene->mMeshes[m];
    for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace& face = mesh->mFaces[f];
      if (face.mNumIndices != 3) continue;  // triangulated by aiProcess_Triangulate
      auto vtx = [&](unsigned i) {
        const aiVector3D& v = mesh->mVertices[face.mIndices[i]];
        return yUpToZUp(v.x, v.y, v.z);
      };
      triangles.push_back(Triangle{vtx(0), vtx(1), vtx(2)});
    }
  }

  addMesh(triangles, matIndex);
  return triangles.size();
}

namespace io {
std::size_t importMesh(Scene& scene, const std::string& path,
                       const std::string& material) {
  return scene.loadMesh(path, material);
}
}  // namespace io

}  // namespace rftrace
