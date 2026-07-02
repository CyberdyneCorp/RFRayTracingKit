#include "rftrace/exporters/json_exporter.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>

#include "rftrace/rf/mimo.hpp"

namespace rftrace::io {

namespace {
using nlohmann::json;

json vec3ToJson(const Vec3& v) { return json::array({v.x(), v.y(), v.z()}); }

Vec3 vec3FromJson(const json& j) {
  return Vec3(j.at(0).get<double>(), j.at(1).get<double>(),
              j.at(2).get<double>());
}

PathType pathTypeFromString(const std::string& s) {
  if (s == "reflection") return PathType::Reflection;
  if (s == "diffraction") return PathType::Diffraction;
  return PathType::LOS;
}
}  // namespace

std::string resultToJsonString(const RFResult& result) {
  json root;
  root["simulation_id"] = result.simulationId;
  root["frequency_hz"] = result.frequencyHz;

  root["transmitters"] = json::array();
  for (const auto& tx : result.transmitters) {
    root["transmitters"].push_back({{"id", tx.id},
                                    {"position", vec3ToJson(tx.position)},
                                    {"frequency_hz", tx.frequencyHz},
                                    {"power_dbm", tx.powerDbm}});
  }

  root["receivers"] = json::array();
  for (const auto& rx : result.receivers) {
    json jr;
    jr["id"] = rx.receiverId;
    jr["position"] = vec3ToJson(rx.position);
    jr["has_signal"] = rx.hasSignal;
    if (rx.hasSignal) {
      jr["received_power_dbm"] = rx.receivedPowerDbm;
      jr["path_loss_db"] = rx.pathLossDb;
      jr["delay_spread_ns"] = rx.delaySpreadNs;
      jr["phase_rad"] = rx.phaseRad;
    } else {
      jr["received_power_dbm"] = nullptr;
      jr["path_loss_db"] = nullptr;
      jr["delay_spread_ns"] = nullptr;
    }

    // Serving-cell / SINR fields are emitted only when SINR was computed
    // (serving id set), so default results keep their archived schema.
    if (!rx.servingTransmitterId.empty()) {
      jr["serving_transmitter_id"] = rx.servingTransmitterId;
      jr["sinr_db"] = std::isfinite(rx.sinrDb) ? json(rx.sinrDb) : json(nullptr);
      jr["interference_power_dbm"] = std::isfinite(rx.interferencePowerDbm)
                                         ? json(rx.interferencePowerDbm)
                                         : json(nullptr);
    }

    jr["paths"] = json::array();
    for (const auto& p : rx.paths) {
      json jp;
      jp["transmitter_id"] = p.transmitterId;
      jp["type"] = toString(p.type);
      jp["points"] = json::array();
      for (const Vec3& pt : p.points) jp["points"].push_back(vec3ToJson(pt));
      jp["power_dbm"] = p.receivedPowerDbm;
      jp["path_loss_db"] = p.pathLossDb;
      jp["phase_rad"] = p.phaseRad;
      jp["delay_ns"] = p.delaySeconds * 1e9;
      jp["reflections"] = p.reflections;
      jp["diffractions"] = p.diffractions;
      jp["material_hits"] = p.materialHits;
      jr["paths"].push_back(std::move(jp));
    }
    root["receivers"].push_back(std::move(jr));
  }

  return root.dump(2);
}

void exportResultJson(const RFResult& result, const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write JSON to '" + path + "'");
  out << resultToJsonString(result);
}

RFResult resultFromJsonString(const std::string& text) {
  const json root = json::parse(text);
  RFResult result;
  result.simulationId = root.value("simulation_id", std::string{});
  result.frequencyHz = root.value("frequency_hz", 0.0);

  if (root.contains("transmitters")) {
    for (const auto& jt : root.at("transmitters")) {
      TransmitterInfo tx;
      tx.id = jt.value("id", std::string{});
      if (jt.contains("position")) tx.position = vec3FromJson(jt.at("position"));
      tx.frequencyHz = jt.value("frequency_hz", 0.0);
      tx.powerDbm = jt.value("power_dbm", 0.0);
      result.transmitters.push_back(std::move(tx));
    }
  }

  if (root.contains("receivers")) {
    for (const auto& jr : root.at("receivers")) {
      ReceiverResult rx;
      rx.receiverId = jr.value("id", std::string{});
      if (jr.contains("position")) rx.position = vec3FromJson(jr.at("position"));
      rx.hasSignal = jr.value("has_signal", false);
      if (rx.hasSignal) {
        rx.receivedPowerDbm = jr.value("received_power_dbm", 0.0);
        rx.pathLossDb = jr.value("path_loss_db", 0.0);
        rx.delaySpreadNs = jr.value("delay_spread_ns", 0.0);
        rx.phaseRad = jr.value("phase_rad", 0.0);
      }
      if (jr.contains("serving_transmitter_id") &&
          !jr.at("serving_transmitter_id").is_null()) {
        rx.servingTransmitterId =
            jr.value("serving_transmitter_id", std::string{});
        if (jr.contains("sinr_db") && !jr.at("sinr_db").is_null())
          rx.sinrDb = jr.at("sinr_db").get<double>();
        if (jr.contains("interference_power_dbm") &&
            !jr.at("interference_power_dbm").is_null())
          rx.interferencePowerDbm =
              jr.at("interference_power_dbm").get<double>();
      }
      if (jr.contains("paths")) {
        for (const auto& jp : jr.at("paths")) {
          RFPath p;
          p.transmitterId = jp.value("transmitter_id", std::string{});
          p.receiverId = rx.receiverId;
          p.type = pathTypeFromString(jp.value("type", std::string{"los"}));
          if (jp.contains("points"))
            for (const auto& pt : jp.at("points"))
              p.points.push_back(vec3FromJson(pt));
          p.receivedPowerDbm = jp.value("power_dbm", 0.0);
          p.pathLossDb = jp.value("path_loss_db", 0.0);
          p.phaseRad = jp.value("phase_rad", 0.0);
          p.delaySeconds = jp.value("delay_ns", 0.0) * 1e-9;
          p.reflections = jp.value("reflections", 0);
          p.diffractions = jp.value("diffractions", 0);
          if (jp.contains("material_hits"))
            p.materialHits =
                jp.at("material_hits").get<std::vector<std::string>>();
          rx.paths.push_back(std::move(p));
        }
      }
      result.receivers.push_back(std::move(rx));
    }
  }
  return result;
}

RFResult loadResultJson(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open JSON file '" + path + "'");
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  return resultFromJsonString(text);
}

namespace {
// Non-finite (no-signal) values serialize as JSON null.
json valueOrNull(double v) { return std::isfinite(v) ? json(v) : json(nullptr); }
}  // namespace

std::string coverageToJsonString(const CoverageResult& coverage) {
  json root;
  root["simulation_id"] = coverage.simulationId;
  root["frequency_hz"] = coverage.frequencyHz;
  root["grid"] = {{"origin", vec3ToJson(coverage.grid.origin)},
                  {"cell_size", coverage.grid.cellSize},
                  {"cols", coverage.grid.cols},
                  {"rows", coverage.grid.rows},
                  {"height", coverage.grid.height}};

  root["power_dbm"] = json::array();
  for (double v : coverage.powerDbm) root["power_dbm"].push_back(valueOrNull(v));
  root["path_loss_db"] = json::array();
  for (double v : coverage.pathLossDb)
    root["path_loss_db"].push_back(valueOrNull(v));

  // SINR array is present only for SINR-enabled coverage runs.
  if (!coverage.sinrDb.empty()) {
    root["sinr_db"] = json::array();
    for (double v : coverage.sinrDb) root["sinr_db"].push_back(valueOrNull(v));
  }

  return root.dump(2);
}

void exportCoverageJson(const CoverageResult& coverage,
                        const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write coverage JSON to '" + path + "'");
  out << coverageToJsonString(coverage);
}

std::string routeToJsonString(const RouteResult& route) {
  json root;
  root["route_id"] = route.routeId;
  root["simulation_id"] = route.simulationId;
  root["frequency_hz"] = route.frequencyHz;

  root["samples"] = json::array();
  for (const auto& s : route.samples) {
    json js;
    js["index"] = s.index;
    js["distance_m"] = s.distanceMeters;
    js["position"] = vec3ToJson(s.position);
    js["has_signal"] = s.hasSignal;
    if (s.hasSignal) {
      js["received_power_dbm"] = s.receivedPowerDbm;
      js["path_loss_db"] = s.pathLossDb;
      js["delay_spread_ns"] = s.delaySpreadNs;
    } else {
      js["received_power_dbm"] = nullptr;
      js["path_loss_db"] = nullptr;
      js["delay_spread_ns"] = nullptr;
    }

    // Serving-cell / SINR fields are emitted only when SINR was computed.
    if (!s.servingTransmitterId.empty()) {
      js["serving_transmitter_id"] = s.servingTransmitterId;
      js["sinr_db"] = valueOrNull(s.sinrDb);
      js["interference_power_dbm"] = valueOrNull(s.interferencePowerDbm);
    }
    root["samples"].push_back(std::move(js));
  }

  return root.dump(2);
}

void exportRouteJson(const RouteResult& route, const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write route JSON to '" + path + "'");
  out << routeToJsonString(route);
}

std::string mimoToJsonString(const Eigen::MatrixXcd& channel,
                             double snrLinear) {
  json root;
  const int rows = static_cast<int>(channel.rows());
  const int cols = static_cast<int>(channel.cols());
  root["rows"] = rows;   // n_rx
  root["cols"] = cols;   // n_tx
  root["snr_linear"] = snrLinear;
  root["capacity_bps_hz"] = rf::capacity(channel, snrLinear);

  json re = json::array(), im = json::array();
  for (int r = 0; r < rows; ++r) {
    json rowRe = json::array(), rowIm = json::array();
    for (int c = 0; c < cols; ++c) {
      rowRe.push_back(channel(r, c).real());
      rowIm.push_back(channel(r, c).imag());
    }
    re.push_back(std::move(rowRe));
    im.push_back(std::move(rowIm));
  }
  root["real"] = std::move(re);
  root["imag"] = std::move(im);
  return root.dump(2);
}

void exportMimoJson(const Eigen::MatrixXcd& channel, double snrLinear,
                    const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write MIMO JSON to '" + path + "'");
  out << mimoToJsonString(channel, snrLinear);
}

}  // namespace rftrace::io
