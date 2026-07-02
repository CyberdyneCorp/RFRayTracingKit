#include "rftrace/importers/material_importer.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string_view>
#include <unordered_map>

#include "rftrace/material.hpp"

namespace rftrace {

namespace materials {
namespace {
// Representative electromagnetic parameters (order-of-magnitude, ITU-R P.2040
// style) for common building/terrain materials. εr/σ drive Fresnel reflection;
// penetration/reflection dB are fallbacks and through-wall losses.
const std::unordered_map<std::string, Material>& presetTable() {
  static const std::unordered_map<std::string, Material> table = {
      {"concrete", {"concrete", 5.31, 0.0326, 0.002, 20.0, 5.0}},
      {"brick", {"brick", 3.75, 0.038, 0.002, 15.0, 4.0}},
      {"glass", {"glass", 6.27, 0.0043, 0.0005, 8.0, 3.0}},
      {"metal", {"metal", 1.0, 1.0e7, 0.0001, 100.0, 0.2}},
      {"wood", {"wood", 1.99, 0.0047, 0.003, 4.0, 6.0}},
      {"water", {"water", 80.0, 0.5, 0.0, 100.0, 2.0}},
      {"vegetation", {"vegetation", 1.5, 0.01, 0.05, 6.0, 8.0}},
      {"asphalt", {"asphalt", 3.18, 0.0018, 0.005, 30.0, 5.0}},
      {"soil", {"soil", 15.0, 0.03, 0.01, 30.0, 4.0}},
  };
  return table;
}
}  // namespace

bool hasPreset(std::string_view name) {
  return presetTable().count(std::string(name)) > 0;
}

Material preset(std::string_view name) {
  const auto& table = presetTable();
  auto it = table.find(std::string(name));
  if (it != table.end()) return it->second;
  Material def;
  def.name = std::string(name);
  return def;
}

}  // namespace materials

namespace {
double pick(const nlohmann::json& e, const char* camel, const char* snake,
            double fallback) {
  if (e.contains(camel)) return e.at(camel).get<double>();
  if (e.contains(snake)) return e.at(snake).get<double>();
  return fallback;
}
}  // namespace

std::size_t Scene::loadMaterials(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw SceneError("cannot open materials file '" + path + "'");

  nlohmann::json j;
  try {
    in >> j;
  } catch (const std::exception& e) {
    throw SceneError(std::string("invalid materials JSON: ") + e.what());
  }

  const nlohmann::json* arr = nullptr;
  if (j.is_array()) {
    arr = &j;
  } else if (j.contains("materials") && j["materials"].is_array()) {
    arr = &j["materials"];
  } else {
    throw SceneError(
        "materials JSON must be an array or an object with a 'materials' array");
  }

  std::size_t count = 0;
  for (const auto& e : *arr) {
    if (!e.contains("name"))
      throw SceneError("material entry missing required 'name' field");
    Material m;
    m.name = e.at("name").get<std::string>();
    m.relativePermittivity =
        pick(e, "relativePermittivity", "relative_permittivity", 1.0);
    m.conductivity = pick(e, "conductivity", "conductivity", 0.0);
    m.roughness = pick(e, "roughness", "roughness", 0.0);
    m.penetrationLossDb =
        pick(e, "penetrationLossDb", "penetration_loss_db", 0.0);
    m.reflectionLossDb = pick(e, "reflectionLossDb", "reflection_loss_db", 0.0);
    addMaterial(m);
    ++count;
  }
  return count;
}

namespace io {
std::size_t importMaterials(Scene& scene, const std::string& path) {
  return scene.loadMaterials(path);
}
}  // namespace io

}  // namespace rftrace
