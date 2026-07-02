#include "rftrace/exporters/json_exporter.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

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

}  // namespace rftrace::io
