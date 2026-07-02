#include "rftrace/importers/cityjson_importer.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "geo_import_util.hpp"
#include "rftrace/geo/footprint.hpp"

namespace rftrace::io {

namespace {
using nlohmann::json;

/// A CityJSON vertex after dequantization: (x=easting/lon, y=northing/lat, z).
struct RealVertex {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

/// True when `node` is a linear ring: a non-empty array of integer indices.
bool isRing(const json& node) {
  return node.is_array() && !node.empty() && node.front().is_number_integer();
}

/// Descend a CityJSON `boundaries` node and collect the outer ring of every
/// surface (interior holes are ignored). Handles MultiSurface/CompositeSurface,
/// Solid and CompositeSolid uniformly by recursing until the surface level (an
/// array whose first element is a ring).
void collectOuterRings(const json& node,
                       std::vector<std::vector<int>>& outerRings) {
  if (!node.is_array() || node.empty()) return;
  if (isRing(node.front())) {
    // `node` is a surface: its first element is the exterior ring.
    outerRings.push_back(node.front().get<std::vector<int>>());
    return;
  }
  for (const json& child : node) collectOuterRings(child, outerRings);
}

/// Fan-triangulate a ring of 3D vertices (drops a repeated closing vertex).
void fanTriangulate(const std::vector<Vec3>& ring, std::vector<Triangle>& out) {
  std::size_t n = ring.size();
  if (n >= 2 && ring.front().x() == ring.back().x() &&
      ring.front().y() == ring.back().y() && ring.front().z() == ring.back().z())
    --n;
  if (n < 3) return;
  for (std::size_t i = 1; i + 1 < n; ++i)
    out.push_back(Triangle{ring[0], ring[i], ring[i + 1]});
}

double attributeHeight(const json& obj) {
  if (!obj.contains("attributes") || !obj.at("attributes").is_object())
    return 0.0;
  const json& attr = obj.at("attributes");
  for (const char* key : {"height", "measuredHeight", "roofHeight"}) {
    if (attr.contains(key) && attr.at(key).is_number())
      return attr.at(key).get<double>();
  }
  return 0.0;
}

std::size_t doImport(Scene& scene, const std::string& path,
                     const std::string& buildingMaterial) {
  std::ifstream in(path);
  if (!in) throw SceneError("cannot open CityJSON file '" + path + "'");

  json doc;
  try {
    in >> doc;
  } catch (const std::exception& e) {
    throw SceneError(std::string("invalid CityJSON: ") + e.what());
  }

  if (!doc.is_object() || doc.value("type", "") != "CityJSON" ||
      !doc.contains("vertices") || !doc.at("vertices").is_array())
    throw SceneError("not a valid CityJSON document (need type=CityJSON and vertices)");
  if (!doc.contains("CityObjects") || !doc.at("CityObjects").is_object())
    throw SceneError("CityJSON has no CityObjects");

  // Dequantize vertices with the transform (defaults to identity).
  double sx = 1.0, sy = 1.0, sz = 1.0, tx = 0.0, ty = 0.0, tz = 0.0;
  if (doc.contains("transform") && doc.at("transform").is_object()) {
    const json& tr = doc.at("transform");
    if (tr.contains("scale") && tr.at("scale").is_array() &&
        tr.at("scale").size() == 3) {
      sx = tr.at("scale")[0].get<double>();
      sy = tr.at("scale")[1].get<double>();
      sz = tr.at("scale")[2].get<double>();
    }
    if (tr.contains("translate") && tr.at("translate").is_array() &&
        tr.at("translate").size() == 3) {
      tx = tr.at("translate")[0].get<double>();
      ty = tr.at("translate")[1].get<double>();
      tz = tr.at("translate")[2].get<double>();
    }
  }

  const json& verticesJson = doc.at("vertices");
  std::vector<RealVertex> vertices;
  vertices.reserve(verticesJson.size());
  for (const json& v : verticesJson) {
    if (!v.is_array() || v.size() < 3) {
      vertices.push_back({});
      continue;
    }
    vertices.push_back({v[0].get<double>() * sx + tx,
                        v[1].get<double>() * sy + ty,
                        v[2].get<double>() * sz + tz});
  }

  // Gather building geometry (outer rings + optional extrusion) before touching
  // the scene, so an error leaves it unchanged.
  struct PendingSurface {
    std::vector<int> ring;
  };
  struct PendingObject {
    std::vector<PendingSurface> surfaces;
    double attrHeight = 0.0;
  };
  std::vector<PendingObject> objects;
  for (const auto& [id, obj] : doc.at("CityObjects").items()) {
    (void)id;
    if (!obj.is_object()) continue;
    const std::string t = obj.value("type", "");
    if (t != "Building" && t != "BuildingPart") continue;
    PendingObject po;
    po.attrHeight = attributeHeight(obj);
    if (obj.contains("geometry") && obj.at("geometry").is_array()) {
      for (const json& geom : obj.at("geometry")) {
        if (!geom.is_object() || !geom.contains("boundaries")) continue;
        std::vector<std::vector<int>> rings;
        collectOuterRings(geom.at("boundaries"), rings);
        for (auto& r : rings) po.surfaces.push_back({std::move(r)});
      }
    }
    if (!po.surfaces.empty()) objects.push_back(std::move(po));
  }

  if (objects.empty())
    throw SceneError("CityJSON contains no building geometry");

  std::vector<std::pair<double, double>> samples;
  for (const PendingObject& po : objects)
    for (const PendingSurface& s : po.surfaces)
      for (int idx : s.ring)
        if (idx >= 0 && idx < static_cast<int>(vertices.size()))
          samples.emplace_back(vertices[idx].y, vertices[idx].x);
  detail::ensureGeoOrigin(scene, samples);
  detail::ensureMaterial(scene, buildingMaterial);

  auto projected = [&](int idx) -> Vec3 {
    const RealVertex& rv = vertices[idx];
    return scene.geoProject(rv.y, rv.x, rv.z);  // x=lon, y=lat, z=alt
  };

  std::vector<Triangle> tris;
  for (const PendingObject& po : objects) {
    // Footprint fallback: a single flat surface with a height attribute is
    // extruded rather than emitted as a bare polygon.
    if (po.surfaces.size() == 1 && po.attrHeight > 0.0) {
      const std::vector<int>& r = po.surfaces[0].ring;
      bool flat = true;
      for (int idx : r)
        if (idx < 0 || idx >= static_cast<int>(vertices.size()) ||
            std::abs(vertices[idx].z - vertices[r.front()].z) > 1e-9) {
          flat = false;
          break;
        }
      if (flat) {
        std::vector<Vec3> ring;
        ring.reserve(r.size());
        for (int idx : r) ring.push_back(projected(idx));
        const double baseZ = ring.empty() ? 0.0 : ring.front().z();
        geo::extrudeFootprint(ring, baseZ, po.attrHeight, tris);
        continue;
      }
    }
    for (const PendingSurface& s : po.surfaces) {
      std::vector<Vec3> ring;
      ring.reserve(s.ring.size());
      for (int idx : s.ring)
        if (idx >= 0 && idx < static_cast<int>(vertices.size()))
          ring.push_back(projected(idx));
      fanTriangulate(ring, tris);
    }
  }

  if (tris.empty()) throw SceneError("CityJSON produced no triangles");
  scene.addMesh(tris, buildingMaterial);
  return tris.size();
}
}  // namespace

std::size_t importCityJSON(Scene& scene, const std::string& path,
                           const std::string& buildingMaterial) {
  return doImport(scene, path, buildingMaterial);
}

}  // namespace rftrace::io

namespace rftrace {
std::size_t Scene::loadCityJSON(const std::string& path,
                                const std::string& buildingMaterial) {
  return io::importCityJSON(*this, path, buildingMaterial);
}
}  // namespace rftrace
