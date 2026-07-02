#include "rftrace/exporters/geojson_exporter.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace rftrace::io {

namespace {
using nlohmann::json;

json featureCollection() {
  return json{{"type", "FeatureCollection"}, {"features", json::array()}};
}

json pointFeature(const Vec3& p, json properties) {
  return json{{"type", "Feature"},
              {"geometry",
               {{"type", "Point"}, {"coordinates", json::array({p.x(), p.y()})}}},
              {"properties", std::move(properties)}};
}

void writeFile(const std::string& text, const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write GeoJSON to '" + path + "'");
  out << text;
}
}  // namespace

std::string receiversToGeoJsonString(const RFResult& result) {
  json fc = featureCollection();
  for (const auto& rx : result.receivers) {
    json props{{"id", rx.receiverId}, {"has_signal", rx.hasSignal}};
    if (rx.hasSignal) {
      props["received_power_dbm"] = rx.receivedPowerDbm;
      props["path_loss_db"] = rx.pathLossDb;
      props["delay_spread_ns"] = rx.delaySpreadNs;
    }
    fc["features"].push_back(pointFeature(rx.position, std::move(props)));
  }
  return fc.dump(2);
}

std::string pathsToGeoJsonString(const RFResult& result) {
  json fc = featureCollection();
  for (const auto& rx : result.receivers) {
    for (const auto& p : rx.paths) {
      json coords = json::array();
      for (const Vec3& pt : p.points)
        coords.push_back(json::array({pt.x(), pt.y(), pt.z()}));
      fc["features"].push_back(
          {{"type", "Feature"},
           {"geometry", {{"type", "LineString"}, {"coordinates", coords}}},
           {"properties",
            {{"type", toString(p.type)},
             {"transmitter_id", p.transmitterId},
             {"receiver_id", p.receiverId},
             {"power_dbm", p.receivedPowerDbm},
             {"reflections", p.reflections}}}});
    }
  }
  return fc.dump(2);
}

std::string coverageToGeoJsonString(const CoverageResult& coverage) {
  json fc = featureCollection();
  const CoverageGrid& g = coverage.grid;
  const double s = g.cellSize;
  for (int row = 0; row < g.rows; ++row) {
    for (int col = 0; col < g.cols; ++col) {
      const double p = coverage.powerDbm[row * g.cols + col];
      if (!std::isfinite(p)) continue;  // omit no-signal cells
      const double x0 = g.origin.x() + col * s;
      const double y0 = g.origin.y() + row * s;
      json ring = json::array({json::array({x0, y0}),
                               json::array({x0 + s, y0}),
                               json::array({x0 + s, y0 + s}),
                               json::array({x0, y0 + s}),
                               json::array({x0, y0})});
      fc["features"].push_back(
          {{"type", "Feature"},
           {"geometry",
            {{"type", "Polygon"}, {"coordinates", json::array({ring})}}},
           {"properties", {{"row", row}, {"col", col}, {"received_power_dbm", p}}}});
    }
  }
  return fc.dump(2);
}

void exportReceiversGeoJson(const RFResult& result, const std::string& path) {
  writeFile(receiversToGeoJsonString(result), path);
}
void exportPathsGeoJson(const RFResult& result, const std::string& path) {
  writeFile(pathsToGeoJsonString(result), path);
}
void exportCoverageGeoJson(const CoverageResult& coverage,
                           const std::string& path) {
  writeFile(coverageToGeoJsonString(coverage), path);
}

}  // namespace rftrace::io
