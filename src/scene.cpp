#include "rftrace/scene.hpp"

#include <cmath>

namespace rftrace {

namespace {
/// Metres per degree of longitude at the equator and per degree of latitude,
/// matching the equirectangular ENU projection used by all geospatial importers.
inline constexpr double kMetersPerDegLonEquator = 111320.0;
inline constexpr double kMetersPerDegLat = 110540.0;
inline constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
}  // namespace

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

void Scene::setGeoOrigin(double latDeg, double lonDeg) {
  coordinateSystem_.originLat = latDeg;
  coordinateSystem_.originLon = lonDeg;
  coordinateSystem_.georeferenced = true;
}

Vec3 Scene::geoProject(double latDeg, double lonDeg, double altMeters) const {
  if (!coordinateSystem_.georeferenced)
    throw SceneError("geoProject called before a geographic origin was set");
  const double x = (lonDeg - coordinateSystem_.originLon) *
                   kMetersPerDegLonEquator *
                   std::cos(coordinateSystem_.originLat * kDegToRad);
  const double y =
      (latDeg - coordinateSystem_.originLat) * kMetersPerDegLat;
  return Vec3(x, y, altMeters);
}

double Scene::groundElevationAt(double x, double y) const {
  if (!terrain_) return 0.0;
  const double e = terrain_->elevationAt(x, y);
  return std::isfinite(e) ? e : 0.0;
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
