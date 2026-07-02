#include "osm_extract.hpp"

#include <algorithm>
#include <exception>
#include <utility>

#include "geo_import_util.hpp"
#include "rftrace/geo/footprint.hpp"

namespace rftrace::io::detail {

namespace {
constexpr double kStoreyHeightM = 3.0;
constexpr double kMinBuildingHeightM = 3.0;

std::string tagValue(const Tags& tags, const std::string& key) {
  auto it = tags.find(key);
  return it == tags.end() ? std::string{} : it->second;
}

/// Building height from OSM tags: `height`, else `building:levels` times the
/// storey height, else the caller default. Numeric parsing tolerates a unit
/// suffix (e.g. "12 m").
double buildingHeight(const Tags& tags, double defaultHeight) {
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

bool isVegetation(const Tags& tags) {
  return tagValue(tags, "natural") == "wood" ||
         tagValue(tags, "landuse") == "forest" ||
         tagValue(tags, "leisure") == "park";
}

struct PendingArea {
  std::vector<LatLon> ring;
  double height = 6.0;
  bool vegetation = false;
};
}  // namespace

std::size_t buildOsmScene(Scene& scene,
                          const std::unordered_map<std::int64_t, LatLon>& nodes,
                          const std::vector<RawWay>& ways,
                          const OsmImportOptions& opts, const char* emptyMsg) {
  std::vector<PendingArea> areas;
  for (const RawWay& w : ways) {
    const bool building = w.tags.count("building") > 0;
    const bool vegetation = isVegetation(w.tags);
    if (!building && !vegetation) continue;

    std::vector<LatLon> ring;
    for (std::int64_t ref : w.refs) {
      auto it = nodes.find(ref);
      if (it != nodes.end()) ring.push_back(it->second);
    }
    if (ring.size() < 3) continue;

    PendingArea area;
    area.ring = std::move(ring);
    area.vegetation = !building;
    area.height = building ? buildingHeight(w.tags, opts.defaultBuildingHeight)
                           : opts.vegetationHeight;
    areas.push_back(std::move(area));
  }

  if (areas.empty()) throw SceneError(emptyMsg);

  std::vector<LatLon> samples;
  for (const PendingArea& a : areas)
    samples.insert(samples.end(), a.ring.begin(), a.ring.end());
  ensureGeoOrigin(scene, samples);
  ensureMaterial(scene, opts.buildingMaterial);
  ensureMaterial(scene, opts.vegetationMaterial);

  std::size_t triangleCount = 0;
  for (const PendingArea& a : areas) {
    std::vector<Vec3> ring;
    ring.reserve(a.ring.size());
    for (const auto& [lat, lon] : a.ring)
      ring.push_back(scene.geoProject(lat, lon, 0.0));
    const double baseZ = footprintBaseZ(scene, ring);
    std::vector<Triangle> tris = geo::extrudeFootprint(ring, baseZ, a.height);
    if (tris.empty()) continue;
    triangleCount += tris.size();
    scene.addMesh(tris, a.vegetation ? opts.vegetationMaterial
                                     : opts.buildingMaterial);
  }
  return triangleCount;
}

}  // namespace rftrace::io::detail
