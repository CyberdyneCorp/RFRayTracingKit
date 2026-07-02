#include "geo_import_util.hpp"

#include "rftrace/importers/material_importer.hpp"

namespace rftrace::io::detail {

void ensureGeoOrigin(Scene& scene,
                     const std::vector<std::pair<double, double>>& latLon) {
  if (scene.hasGeoOrigin() || latLon.empty()) return;
  double sumLat = 0.0;
  double sumLon = 0.0;
  for (const auto& [lat, lon] : latLon) {
    sumLat += lat;
    sumLon += lon;
  }
  const double n = static_cast<double>(latLon.size());
  scene.setGeoOrigin(sumLat / n, sumLon / n);
}

void ensureMaterial(Scene& scene, const std::string& name) {
  if (name.empty()) return;
  if (scene.materialIndex(name).has_value()) return;
  // materials::preset falls back to a neutral default carrying `name` when the
  // name is not a known preset, so this always yields a usable material.
  scene.addMaterial(materials::preset(name));
}

double footprintBaseZ(const Scene& scene, const std::vector<Vec3>& ringLocal) {
  if (!scene.offsetBuildingBases() || !scene.hasTerrain() || ringLocal.empty())
    return 0.0;
  double cx = 0.0;
  double cy = 0.0;
  for (const Vec3& v : ringLocal) {
    cx += v.x();
    cy += v.y();
  }
  const double n = static_cast<double>(ringLocal.size());
  return scene.groundElevationAt(cx / n, cy / n);
}

}  // namespace rftrace::io::detail
