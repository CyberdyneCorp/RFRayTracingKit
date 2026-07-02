#include "rftrace/scene.hpp"

namespace rftrace {

Scene::Scene() {
  // Index 0 is always a neutral default material used for unassigned meshes.
  Material def;
  def.name = "default";
  def.reflectionLossDb = 6.0;
  def.penetrationLossDb = 20.0;
  materials_.push_back(def);
  materialByName_[def.name] = 0;
  defaultMaterialIndex_ = 0;
}

int Scene::addMaterial(const Material& material) {
  auto it = materialByName_.find(material.name);
  if (it != materialByName_.end()) {
    materials_[it->second] = material;
    return it->second;
  }
  const int index = static_cast<int>(materials_.size());
  materials_.push_back(material);
  materialByName_[material.name] = index;
  return index;
}

std::optional<int> Scene::materialIndex(const std::string& name) const {
  auto it = materialByName_.find(name);
  if (it == materialByName_.end()) return std::nullopt;
  return it->second;
}

void Scene::addMesh(const std::vector<Triangle>& triangles,
                    const std::string& materialName) {
  int index = defaultMaterialIndex_;
  if (!materialName.empty()) {
    auto found = materialIndex(materialName);
    if (!found)
      throw SceneError("unknown material '" + materialName + "'");
    index = *found;
  }
  addMesh(triangles, index);
}

void Scene::addMesh(const std::vector<Triangle>& triangles, int materialIndex) {
  const int index =
      (materialIndex < 0 || materialIndex >= static_cast<int>(materials_.size()))
          ? defaultMaterialIndex_
          : materialIndex;
  triangles_.reserve(triangles_.size() + triangles.size());
  triangleMaterial_.reserve(triangleMaterial_.size() + triangles.size());
  for (const Triangle& t : triangles) {
    triangles_.push_back(t);
    triangleMaterial_.push_back(index);
  }
}

void Scene::addTransmitter(const Transmitter& tx) {
  if (txById_.count(tx.id))
    throw SceneError("duplicate transmitter id '" + tx.id + "'");
  txById_[tx.id] = transmitters_.size();
  transmitters_.push_back(tx);
}

void Scene::addReceiver(const Receiver& rx) {
  if (rxById_.count(rx.id))
    throw SceneError("duplicate receiver id '" + rx.id + "'");
  rxById_[rx.id] = receivers_.size();
  receivers_.push_back(rx);
}

}  // namespace rftrace
