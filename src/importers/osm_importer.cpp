#include "rftrace/importers/osm_importer.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "geo_import_util.hpp"
#include "rftrace/geo/footprint.hpp"

namespace rftrace::io {

namespace {
using nlohmann::json;

constexpr double kStoreyHeightM = 3.0;
constexpr double kMinBuildingHeightM = 3.0;

using LatLon = std::pair<double, double>;

std::string tagValue(const json& tags, const char* key) {
  if (tags.is_object() && tags.contains(key) && tags.at(key).is_string())
    return tags.at(key).get<std::string>();
  return {};
}

/// Building height from OSM tags: `height`, else `building:levels` times the
/// storey height, else the caller default. Numeric parsing tolerates a unit
/// suffix (e.g. "12 m").
double buildingHeight(const json& tags, double defaultHeight) {
  const std::string h = tagValue(tags, "height");
  if (!h.empty()) {
    try {
      return std::max(kMinBuildingHeightM, std::stod(h));
    } catch (const std::exception&) {
    }
  }
  const std::string levels = tagValue(tags, "building:levels");
  if (!levels.empty()) {
    try {
      return std::max(kMinBuildingHeightM, std::stod(levels) * kStoreyHeightM);
    } catch (const std::exception&) {
    }
  }
  return defaultHeight;
}

bool isVegetation(const json& tags) {
  return tagValue(tags, "natural") == "wood" ||
         tagValue(tags, "landuse") == "forest" ||
         tagValue(tags, "leisure") == "park";
}

struct PendingArea {
  std::vector<LatLon> ring;
  double height = 6.0;
  bool vegetation = false;
};

std::size_t doImport(Scene& scene, const std::string& path,
                     const OsmImportOptions& opts) {
  std::ifstream in(path);
  if (!in) throw SceneError("cannot open Overpass JSON file '" + path + "'");

  json doc;
  try {
    in >> doc;
  } catch (const std::exception& e) {
    throw SceneError(std::string("invalid Overpass JSON: ") + e.what());
  }
  if (!doc.is_object() || !doc.contains("elements") ||
      !doc.at("elements").is_array())
    throw SceneError("not a valid Overpass JSON document (need an 'elements' array)");

  const json& elements = doc.at("elements");

  // Resolve node id -> (lat, lon).
  std::unordered_map<std::int64_t, LatLon> nodes;
  for (const json& e : elements) {
    if (e.value("type", "") == "node" && e.contains("id") &&
        e.contains("lat") && e.contains("lon"))
      nodes[e.at("id").get<std::int64_t>()] = {e.at("lat").get<double>(),
                                               e.at("lon").get<double>()};
  }

  std::vector<PendingArea> areas;
  for (const json& e : elements) {
    if (e.value("type", "") != "way" || !e.contains("nodes")) continue;
    const json& tags =
        e.contains("tags") && e.at("tags").is_object() ? e.at("tags")
                                                        : json::object();
    const bool building = tags.is_object() && tags.contains("building");
    const bool vegetation = isVegetation(tags);
    if (!building && !vegetation) continue;

    std::vector<LatLon> ring;
    for (const json& n : e.at("nodes")) {
      auto it = nodes.find(n.get<std::int64_t>());
      if (it != nodes.end()) ring.push_back(it->second);
    }
    if (ring.size() < 3) continue;

    PendingArea area;
    area.ring = std::move(ring);
    area.vegetation = building ? false : true;
    area.height = building ? buildingHeight(tags, opts.defaultBuildingHeight)
                           : opts.vegetationHeight;
    areas.push_back(std::move(area));
  }

  if (areas.empty())
    throw SceneError("Overpass JSON contains no building or vegetation ways");

  std::vector<LatLon> samples;
  for (const PendingArea& a : areas)
    samples.insert(samples.end(), a.ring.begin(), a.ring.end());
  detail::ensureGeoOrigin(scene, samples);
  detail::ensureMaterial(scene, opts.buildingMaterial);
  detail::ensureMaterial(scene, opts.vegetationMaterial);

  std::size_t triangleCount = 0;
  for (const PendingArea& a : areas) {
    std::vector<Vec3> ring;
    ring.reserve(a.ring.size());
    for (const auto& [lat, lon] : a.ring)
      ring.push_back(scene.geoProject(lat, lon, 0.0));
    const double baseZ = detail::footprintBaseZ(scene, ring);
    std::vector<Triangle> tris = geo::extrudeFootprint(ring, baseZ, a.height);
    if (tris.empty()) continue;
    triangleCount += tris.size();
    scene.addMesh(tris, a.vegetation ? opts.vegetationMaterial
                                     : opts.buildingMaterial);
  }
  return triangleCount;
}
}  // namespace

std::size_t importOSM(Scene& scene, const std::string& path,
                      const OsmImportOptions& opts) {
  return doImport(scene, path, opts);
}

}  // namespace rftrace::io

namespace rftrace {
std::size_t Scene::loadOSM(const std::string& path,
                           const std::string& buildingMaterial,
                           const std::string& vegetationMaterial) {
  io::OsmImportOptions opts;
  opts.buildingMaterial = buildingMaterial;
  opts.vegetationMaterial = vegetationMaterial;
  return io::importOSM(*this, path, opts);
}
}  // namespace rftrace
