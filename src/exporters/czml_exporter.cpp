#include "rftrace/exporters/czml_exporter.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

#include "rftrace/scene.hpp"

namespace rftrace::io {

namespace {
using nlohmann::json;

inline constexpr double kMetersPerDegLonEquator = 111320.0;
inline constexpr double kMetersPerDegLat = 110540.0;
inline constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

/// How positions are written into the CZML document.
struct Georef {
  bool enabled = false;
  double originLat = 0.0;
  double originLon = 0.0;
};

/// Inverse of the D1 equirectangular projection: local ENU metres -> lon/lat/alt.
void toLonLatAlt(const Georef& g, const Vec3& p, double& lon, double& lat,
                 double& alt) {
  const double mPerDegLon =
      kMetersPerDegLonEquator * std::cos(g.originLat * kDegToRad);
  lon = g.originLon + p.x() / mPerDegLon;
  lat = g.originLat + p.y() / kMetersPerDegLat;
  alt = p.z();
}

/// Encode a single position as a CZML `position` value.
json positionValue(const Georef& g, const Vec3& p) {
  if (g.enabled) {
    double lon = 0.0, lat = 0.0, alt = 0.0;
    toLonLatAlt(g, p, lon, lat, alt);
    return json{{"cartographicDegrees", json::array({lon, lat, alt})}};
  }
  return json{{"cartesian", json::array({p.x(), p.y(), p.z()})}};
}

/// Encode a polyline's vertices as a flat CZML positions value.
json polylinePositions(const Georef& g, const std::vector<Vec3>& points) {
  json flat = json::array();
  for (const Vec3& p : points) {
    if (g.enabled) {
      double lon = 0.0, lat = 0.0, alt = 0.0;
      toLonLatAlt(g, p, lon, lat, alt);
      flat.push_back(lon);
      flat.push_back(lat);
      flat.push_back(alt);
    } else {
      flat.push_back(p.x());
      flat.push_back(p.y());
      flat.push_back(p.z());
    }
  }
  const char* key = g.enabled ? "cartographicDegrees" : "cartesian";
  return json{{key, std::move(flat)}};
}

/// RGBA (0-255) for a path/point coloured by received power: blue -> red.
json powerRgba(double powerDbm, int alpha) {
  const double lo = -120.0, hi = -40.0;
  double t = (powerDbm - lo) / (hi - lo);
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;
  const int r = static_cast<int>(t * 255.0);
  const int b = static_cast<int>((1.0 - t) * 255.0);
  return json::array({r, 60, b, alpha});
}

std::string buildCzml(const RFResult& result, const Georef& g) {
  json doc = json::array();

  // Document packet.
  doc.push_back({{"id", "document"},
                 {"name", result.simulationId.empty() ? "RFTraceKit"
                                                       : result.simulationId},
                 {"version", "1.0"}});

  // One point packet per receiver.
  for (const auto& rx : result.receivers) {
    std::string desc = rx.hasSignal
                           ? "Received power: " +
                                 std::to_string(rx.receivedPowerDbm) + " dBm"
                           : "No signal";
    doc.push_back(
        {{"id", "receiver/" + rx.receiverId},
         {"name", rx.receiverId},
         {"description", desc},
         {"position", positionValue(g, rx.position)},
         {"point",
          {{"pixelSize", 10},
           {"color",
            {{"rgba", rx.hasSignal ? powerRgba(rx.receivedPowerDbm, 255)
                                   : json::array({128, 128, 128, 255})}}}}}});
  }

  // One polyline packet per ray path.
  std::size_t pathIndex = 0;
  for (const auto& rx : result.receivers) {
    for (const auto& p : rx.paths) {
      doc.push_back(
          {{"id", "path/" + std::to_string(pathIndex)},
           {"name", toString(p.type)},
           {"polyline",
            {{"positions", polylinePositions(g, p.points)},
             {"width", 2},
             {"material",
              {{"solidColor",
                {{"color", {{"rgba", powerRgba(p.receivedPowerDbm, 200)}}}}}}}}}});
      ++pathIndex;
    }
  }

  return doc.dump(2);
}

void writeFile(const std::string& text, const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write CZML to '" + path + "'");
  out << text;
}
}  // namespace

std::string resultToCzmlString(const RFResult& result) {
  return buildCzml(result, Georef{});
}

std::string resultToCzmlString(const RFResult& result, const Scene& scene) {
  const CoordinateSystem& cs = scene.coordinateSystem();
  Georef g{cs.georeferenced, cs.originLat, cs.originLon};
  return buildCzml(result, g);
}

void exportResultCzml(const RFResult& result, const std::string& path) {
  writeFile(resultToCzmlString(result), path);
}

void exportResultCzml(const RFResult& result, const std::string& path,
                      const Scene& scene) {
  writeFile(resultToCzmlString(result, scene), path);
}

}  // namespace rftrace::io
