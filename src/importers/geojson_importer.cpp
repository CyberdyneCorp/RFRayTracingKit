#include "rftrace/importers/geojson_importer.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
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

constexpr double kStoreyHeightM = 3.0;  ///< assumed metres per building storey
constexpr double kMinBuildingHeightM = 3.0;

/// Parse a JSON value that may be a number or a numeric string. Returns false
/// when it is neither.
bool asNumber(const json& v, double& out) {
  if (v.is_number()) {
    out = v.get<double>();
    return true;
  }
  if (v.is_string()) {
    try {
      out = std::stod(v.get<std::string>());
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }
  return false;
}

/// Building height from feature properties: `height`, else `levels`/
/// `building:levels` times the storey height, else the caller default.
double heightFromProps(const json& props, double defaultHeight) {
  double h = 0.0;
  if (props.contains("height") && asNumber(props.at("height"), h) && h > 0.0)
    return std::max(kMinBuildingHeightM, h);
  for (const char* key : {"levels", "building:levels"}) {
    double levels = 0.0;
    if (props.contains(key) && asNumber(props.at(key), levels) && levels > 0.0)
      return std::max(kMinBuildingHeightM, levels * kStoreyHeightM);
  }
  return defaultHeight;
}

using LatLon = std::pair<double, double>;  // (lat, lon)

/// A building footprint parsed from a feature, before georeferenced projection.
struct PendingBuilding {
  std::vector<LatLon> ring;
  double height = 6.0;
  std::string material;
};

/// A point feature parsed from the collection, before projection.
struct PendingPoint {
  double lat = 0.0;
  double lon = 0.0;
  double alt = 0.0;
  std::string id;
  bool transmitter = false;
};

/// Read a GeoJSON position array `[lon, lat, (alt)]` into lat/lon. Returns
/// false when the array does not hold at least two numbers.
bool readPosition(const json& pos, double& lat, double& lon) {
  if (!pos.is_array() || pos.size() < 2 || !pos[0].is_number() ||
      !pos[1].is_number())
    return false;
  lon = pos[0].get<double>();
  lat = pos[1].get<double>();
  return true;
}

/// Convert a GeoJSON linear ring (`[[lon,lat],...]`) into a lat/lon ring.
std::vector<LatLon> parseRing(const json& ringJson) {
  std::vector<LatLon> ring;
  if (!ringJson.is_array()) return ring;
  ring.reserve(ringJson.size());
  for (const json& pos : ringJson) {
    double lat = 0.0;
    double lon = 0.0;
    if (readPosition(pos, lat, lon)) ring.emplace_back(lat, lon);
  }
  return ring;
}

/// Append a Polygon's outer ring (`coordinates[0]`) as a pending building.
void addPolygon(const json& coordinates, const json& props,
                const GeoJsonImportOptions& opts,
                std::vector<PendingBuilding>& out) {
  if (!coordinates.is_array() || coordinates.empty()) return;
  std::vector<LatLon> ring = parseRing(coordinates[0]);
  if (ring.size() < 3) return;
  std::string material = opts.buildingMaterial;
  if (props.contains("material") && props.at("material").is_string())
    material = props.at("material").get<std::string>();
  out.push_back({std::move(ring), heightFromProps(props, opts.defaultHeight),
                 std::move(material)});
}

bool pointIsTransmitter(const json& props, const GeoJsonImportOptions& opts) {
  for (const char* key : {"type", "layer"}) {
    if (props.contains(key) && props.at(key).is_string()) {
      const std::string v = props.at(key).get<std::string>();
      if (v == "transmitter" || v == "tx") return true;
      if (v == "receiver" || v == "rx") return false;
    }
  }
  return opts.pointType == "transmitter" || opts.pointType == "tx";
}

void collectFeature(const json& feature, const GeoJsonImportOptions& opts,
                    std::vector<PendingBuilding>& buildings,
                    std::vector<PendingPoint>& points) {
  if (!feature.is_object() || !feature.contains("geometry")) return;
  const json& geom = feature.at("geometry");
  if (!geom.is_object() || !geom.contains("type") ||
      !geom.contains("coordinates"))
    return;
  const json props =
      feature.contains("properties") && feature.at("properties").is_object()
          ? feature.at("properties")
          : json::object();
  const std::string type = geom.at("type").get<std::string>();
  const json& coords = geom.at("coordinates");

  if (type == "Polygon") {
    addPolygon(coords, props, opts, buildings);
  } else if (type == "MultiPolygon") {
    for (const json& polygon : coords) addPolygon(polygon, props, opts, buildings);
  } else if (type == "Point") {
    if (opts.pointType.empty()) return;
    double lat = 0.0;
    double lon = 0.0;
    if (!readPosition(coords, lat, lon)) return;
    double alt = coords.size() >= 3 && coords[2].is_number()
                     ? coords[2].get<double>()
                     : 0.0;
    PendingPoint p;
    p.lat = lat;
    p.lon = lon;
    p.alt = alt;
    p.transmitter = pointIsTransmitter(props, opts);
    for (const char* key : {"id", "name"}) {
      if (props.contains(key) && props.at(key).is_string()) {
        p.id = props.at(key).get<std::string>();
        break;
      }
    }
    points.push_back(std::move(p));
  }
}

std::size_t doImport(Scene& scene, const std::string& path,
                     const GeoJsonImportOptions& opts) {
  std::ifstream in(path);
  if (!in) throw SceneError("cannot open GeoJSON file '" + path + "'");

  json doc;
  try {
    in >> doc;
  } catch (const std::exception& e) {
    throw SceneError(std::string("invalid GeoJSON: ") + e.what());
  }

  if (!doc.is_object() || !doc.contains("type") ||
      doc.at("type") != "FeatureCollection" || !doc.contains("features") ||
      !doc.at("features").is_array())
    throw SceneError("GeoJSON is not a FeatureCollection");

  std::vector<PendingBuilding> buildings;
  std::vector<PendingPoint> points;
  for (const json& feature : doc.at("features"))
    collectFeature(feature, opts, buildings, points);

  if (buildings.empty() && points.empty())
    throw SceneError("GeoJSON contains no usable geometry");

  // Nothing above mutates the scene, so an error leaves it unchanged.
  std::vector<LatLon> samples;
  for (const PendingBuilding& b : buildings)
    samples.insert(samples.end(), b.ring.begin(), b.ring.end());
  for (const PendingPoint& p : points) samples.emplace_back(p.lat, p.lon);
  detail::ensureGeoOrigin(scene, samples);

  std::size_t triangleCount = 0;
  for (const PendingBuilding& b : buildings) {
    std::vector<Vec3> ring;
    ring.reserve(b.ring.size());
    for (const auto& [lat, lon] : b.ring)
      ring.push_back(scene.geoProject(lat, lon, 0.0));
    const double baseZ = detail::footprintBaseZ(scene, ring);
    std::vector<Triangle> tris = geo::extrudeFootprint(ring, baseZ, b.height);
    if (tris.empty()) continue;
    detail::ensureMaterial(scene, b.material);
    triangleCount += tris.size();
    scene.addMesh(tris, b.material);
  }

  std::size_t rxAuto = scene.receivers().size();
  std::size_t txAuto = scene.transmitters().size();
  for (const PendingPoint& p : points) {
    const Vec3 pos = scene.geoProject(p.lat, p.lon, p.alt);
    if (p.transmitter) {
      Transmitter tx;
      tx.id = p.id.empty() ? "tx_" + std::to_string(txAuto++) : p.id;
      tx.position = pos;
      scene.addTransmitter(tx);
    } else {
      Receiver rx;
      rx.id = p.id.empty() ? "rx_" + std::to_string(rxAuto++) : p.id;
      rx.position = pos;
      scene.addReceiver(rx);
    }
  }
  return triangleCount;
}
}  // namespace

std::size_t importGeoJSON(Scene& scene, const std::string& path,
                          const GeoJsonImportOptions& opts) {
  return doImport(scene, path, opts);
}

}  // namespace rftrace::io

namespace rftrace {
std::size_t Scene::loadGeoJSON(const std::string& path,
                               const std::string& buildingMaterial,
                               const std::string& pointType) {
  io::GeoJsonImportOptions opts;
  opts.buildingMaterial = buildingMaterial;
  opts.pointType = pointType;
  return io::importGeoJSON(*this, path, opts);
}
}  // namespace rftrace
