#include "rftrace/exporters/gltf_exporter.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace rftrace::io {

namespace {
using nlohmann::json;

// glTF constants.
constexpr int kFloat = 5126;
constexpr int kArrayBuffer = 34962;
constexpr int kModePoints = 0;
constexpr int kModeLineStrip = 3;

std::string base64Encode(const std::vector<std::uint8_t>& data) {
  static const char* tbl =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 3 <= data.size(); i += 3) {
    const std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out.push_back(tbl[(n >> 18) & 63]);
    out.push_back(tbl[(n >> 12) & 63]);
    out.push_back(tbl[(n >> 6) & 63]);
    out.push_back(tbl[n & 63]);
  }
  if (i + 1 == data.size()) {
    const std::uint32_t n = data[i] << 16;
    out.push_back(tbl[(n >> 18) & 63]);
    out.push_back(tbl[(n >> 12) & 63]);
    out.push_back('=');
    out.push_back('=');
  } else if (i + 2 == data.size()) {
    const std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
    out.push_back(tbl[(n >> 18) & 63]);
    out.push_back(tbl[(n >> 12) & 63]);
    out.push_back(tbl[(n >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

void appendFloat(std::vector<std::uint8_t>& buf, float f) {
  std::uint8_t bytes[4];
  std::memcpy(bytes, &f, 4);
  buf.insert(buf.end(), bytes, bytes + 4);
}

/// Map received power (dBm) to an RGBA color: blue (weak) → red (strong).
std::array<float, 4> powerColor(double powerDbm) {
  const double lo = -120.0, hi = -40.0;
  double t = (powerDbm - lo) / (hi - lo);
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;
  return {static_cast<float>(t), static_cast<float>(0.2),
          static_cast<float>(1.0 - t), 1.0f};
}

struct Primitive {
  int mode;
  std::vector<Vec3> positions;
  std::vector<std::array<float, 4>> colors;
};

}  // namespace

std::string pathsToGltfString(const RFResult& result, bool includeReceivers) {
  std::vector<Primitive> prims;

  for (const auto& rx : result.receivers) {
    for (const auto& p : rx.paths) {
      if (p.points.size() < 2) continue;
      Primitive prim;
      prim.mode = kModeLineStrip;
      const auto color = powerColor(p.receivedPowerDbm);
      for (const Vec3& pt : p.points) {
        prim.positions.push_back(pt);
        prim.colors.push_back(color);
      }
      prims.push_back(std::move(prim));
    }
  }

  if (includeReceivers && !result.receivers.empty()) {
    Primitive pts;
    pts.mode = kModePoints;
    for (const auto& rx : result.receivers) {
      pts.positions.push_back(rx.position);
      pts.colors.push_back({1.0f, 1.0f, 1.0f, 1.0f});
    }
    if (!pts.positions.empty()) prims.push_back(std::move(pts));
  }

  // Serialize interleaved-by-primitive: positions block then colors block.
  std::vector<std::uint8_t> buffer;
  json bufferViews = json::array();
  json accessors = json::array();
  json jsonPrims = json::array();

  for (const Primitive& prim : prims) {
    const std::size_t n = prim.positions.size();

    // POSITION.
    const std::size_t posOffset = buffer.size();
    Vec3 lo{std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()};
    Vec3 hi = -lo;
    for (const Vec3& v : prim.positions) {
      appendFloat(buffer, static_cast<float>(v.x()));
      appendFloat(buffer, static_cast<float>(v.y()));
      appendFloat(buffer, static_cast<float>(v.z()));
      lo = lo.cwiseMin(v);
      hi = hi.cwiseMax(v);
    }
    const int posView = static_cast<int>(bufferViews.size());
    bufferViews.push_back({{"buffer", 0},
                           {"byteOffset", posOffset},
                           {"byteLength", n * 12},
                           {"target", kArrayBuffer}});
    const int posAcc = static_cast<int>(accessors.size());
    accessors.push_back({{"bufferView", posView},
                         {"componentType", kFloat},
                         {"count", n},
                         {"type", "VEC3"},
                         {"min", json::array({lo.x(), lo.y(), lo.z()})},
                         {"max", json::array({hi.x(), hi.y(), hi.z()})}});

    // COLOR_0.
    const std::size_t colOffset = buffer.size();
    for (const auto& c : prim.colors)
      for (float ch : c) appendFloat(buffer, ch);
    const int colView = static_cast<int>(bufferViews.size());
    bufferViews.push_back({{"buffer", 0},
                           {"byteOffset", colOffset},
                           {"byteLength", n * 16},
                           {"target", kArrayBuffer}});
    const int colAcc = static_cast<int>(accessors.size());
    accessors.push_back({{"bufferView", colView},
                         {"componentType", kFloat},
                         {"count", n},
                         {"type", "VEC4"}});

    jsonPrims.push_back(
        {{"attributes", {{"POSITION", posAcc}, {"COLOR_0", colAcc}}},
         {"mode", prim.mode}});
  }

  json root;
  root["asset"] = {{"version", "2.0"}, {"generator", "RFTraceKit"}};
  root["scene"] = 0;
  root["scenes"] = json::array({{{"nodes", json::array({0})}}});
  root["nodes"] = json::array({{{"mesh", 0}}});
  root["meshes"] = json::array({{{"primitives", jsonPrims}}});
  root["accessors"] = accessors;
  root["bufferViews"] = bufferViews;
  root["buffers"] = json::array(
      {{{"byteLength", buffer.size()},
        {"uri", "data:application/octet-stream;base64," + base64Encode(buffer)}}});

  return root.dump(2);
}

void exportPathsGltf(const RFResult& result, const std::string& path,
                     bool includeReceivers) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write glTF to '" + path + "'");
  out << pathsToGltfString(result, includeReceivers);
}

}  // namespace rftrace::io
