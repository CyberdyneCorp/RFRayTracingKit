#include "rftrace/exporters/tiles3d_exporter.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rftrace/exporters/gltf_exporter.hpp"

namespace rftrace::io {

namespace {
using nlohmann::json;

// GLB container constants (glTF 2.0 binary).
constexpr std::uint32_t kGlbMagic = 0x46546C67;      // "glTF"
constexpr std::uint32_t kGlbVersion = 2;
constexpr std::uint32_t kChunkTypeJson = 0x4E4F534A;  // "JSON"
constexpr std::uint32_t kChunkTypeBin = 0x004E4942;   // "BIN\0"

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

std::vector<std::uint8_t> base64Decode(const std::string& in) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;  // padding or ignorable
  };
  std::vector<std::uint8_t> out;
  int buf = 0, bits = 0;
  for (char c : in) {
    const int v = value(c);
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF));
    }
  }
  return out;
}

/// Convert the (self-contained, data-URI-buffered) glTF JSON from the path
/// exporter into a proper GLB: the embedded base64 buffer is decoded into a real
/// binary chunk and the buffer's `uri` dropped, which is what GLB readers such as
/// Assimp expect.
std::vector<std::uint8_t> gltfJsonToGlb(const std::string& gltfJson) {
  json doc = json::parse(gltfJson);

  std::vector<std::uint8_t> bin;
  if (doc.contains("buffers") && doc["buffers"].is_array() &&
      !doc["buffers"].empty()) {
    json& buffer = doc["buffers"][0];
    if (buffer.contains("uri")) {
      const std::string uri = buffer["uri"].get<std::string>();
      const auto comma = uri.find(',');
      const std::string payload =
          comma == std::string::npos ? uri : uri.substr(comma + 1);
      bin = base64Decode(payload);
      buffer.erase("uri");                    // GLB buffer references the BIN chunk
      buffer["byteLength"] = bin.size();
    }
  }

  const std::string jsonStr = doc.dump();
  std::vector<std::uint8_t> jsonChunk(jsonStr.begin(), jsonStr.end());
  while (jsonChunk.size() % 4 != 0) jsonChunk.push_back(0x20);  // pad with spaces
  while (bin.size() % 4 != 0) bin.push_back(0x00);              // pad with zeros

  std::uint32_t total = 12 + 8 + static_cast<std::uint32_t>(jsonChunk.size());
  if (!bin.empty()) total += 8 + static_cast<std::uint32_t>(bin.size());

  std::vector<std::uint8_t> glb;
  glb.reserve(total);
  putU32(glb, kGlbMagic);
  putU32(glb, kGlbVersion);
  putU32(glb, total);
  putU32(glb, static_cast<std::uint32_t>(jsonChunk.size()));
  putU32(glb, kChunkTypeJson);
  glb.insert(glb.end(), jsonChunk.begin(), jsonChunk.end());
  if (!bin.empty()) {
    putU32(glb, static_cast<std::uint32_t>(bin.size()));
    putU32(glb, kChunkTypeBin);
    glb.insert(glb.end(), bin.begin(), bin.end());
  }
  return glb;
}

/// Axis-aligned bounds over every path vertex and receiver position.
struct Bounds {
  Vec3 lo{0, 0, 0};
  Vec3 hi{0, 0, 0};
  bool any = false;

  void add(const Vec3& p) {
    if (!any) {
      lo = hi = p;
      any = true;
    } else {
      lo = lo.cwiseMin(p);
      hi = hi.cwiseMax(p);
    }
  }
};

Bounds computeBounds(const RFResult& result) {
  Bounds b;
  for (const auto& rx : result.receivers) {
    b.add(rx.position);
    for (const auto& p : rx.paths)
      for (const Vec3& pt : p.points) b.add(pt);
  }
  if (!b.any) {  // degenerate: keep a valid unit box
    b.lo = Vec3(-0.5, -0.5, -0.5);
    b.hi = Vec3(0.5, 0.5, 0.5);
  }
  return b;
}

// --- Hierarchical LOD helpers (D5) -----------------------------------------

/// Horizontal (X/Y) cell of a quadtree node. The vertical (Z) extent is shared
/// across all tiles (the scene's global Z range), so child boxes always nest
/// inside their parent's box.
struct HCell {
  double xlo, xhi, ylo, yhi;
};

/// Sub-result holding only the receivers (with their full ray paths) whose
/// horizontal position falls inside `cell`. Ownership is by receiver position;
/// a receiver's paths are carried whole even if they reach outside the cell.
RFResult filterByCell(const RFResult& result, const HCell& cell) {
  RFResult out;
  out.simulationId = result.simulationId;
  out.frequencyHz = result.frequencyHz;
  out.transmitters = result.transmitters;
  for (const auto& rx : result.receivers) {
    const double x = rx.position.x();
    const double y = rx.position.y();
    if (x >= cell.xlo && x <= cell.xhi && y >= cell.ylo && y <= cell.yhi)
      out.receivers.push_back(rx);
  }
  return out;
}

/// True if the glTF writer would emit at least one primitive for `subset`
/// (a polyline path with >= 2 points, or a receiver point when included).
bool hasRenderable(const RFResult& subset, bool includeReceivers) {
  for (const auto& rx : subset.receivers)
    for (const auto& p : rx.paths)
      if (p.points.size() >= 2) return true;
  return includeReceivers && !subset.receivers.empty();
}

/// 3D Tiles 1.1 `box`: [center(3), x-halfaxis(3), y-halfaxis(3), z-halfaxis(3)].
/// A tiny floor on each half-axis keeps degenerate (flat) cells valid.
json cellBox(const HCell& cell, double zlo, double zhi) {
  constexpr double kEps = 1e-6;
  const double cx = 0.5 * (cell.xlo + cell.xhi);
  const double cy = 0.5 * (cell.ylo + cell.yhi);
  const double cz = 0.5 * (zlo + zhi);
  const double hx = std::max(0.5 * (cell.xhi - cell.xlo), kEps);
  const double hy = std::max(0.5 * (cell.yhi - cell.ylo), kEps);
  const double hz = std::max(0.5 * (zhi - zlo), kEps);
  return json::array({cx, cy, cz, hx, 0.0, 0.0, 0.0, hy, 0.0, 0.0, 0.0, hz});
}

void writeGlbFile(const RFResult& subset, const std::filesystem::path& glbPath,
                  bool includeReceivers) {
  const std::vector<std::uint8_t> glb =
      gltfJsonToGlb(pathsToGltfString(subset, includeReceivers));
  std::ofstream out(glbPath, std::ios::binary);
  if (!out)
    throw std::runtime_error("cannot write GLB to '" + glbPath.string() + "'");
  out.write(reinterpret_cast<const char*>(glb.data()),
            static_cast<std::streamsize>(glb.size()));
}

/// The four horizontal quadrants of `cell`, split at its midpoint.
std::array<HCell, 4> quadrants(const HCell& cell) {
  const double mx = 0.5 * (cell.xlo + cell.xhi);
  const double my = 0.5 * (cell.ylo + cell.yhi);
  return {HCell{cell.xlo, mx, cell.ylo, my}, HCell{mx, cell.xhi, cell.ylo, my},
          HCell{cell.xlo, mx, my, cell.yhi}, HCell{mx, cell.xhi, my, cell.yhi}};
}

/// Shared state for one LOD export, threaded through the recursion.
struct LodContext {
  std::filesystem::path dir;
  double zlo, zhi;
  double baseError;
  int maxDepth;
  bool includeReceivers;
};

/// Recursively build one tile (and its subtree). Returns std::nullopt when the
/// tile and all of its descendants are empty (so empty quadrants are dropped).
/// `key` uniquely names this tile's content file (e.g. "r", "r0", "r03").
std::optional<json> buildLodTile(const RFResult& result, const LodContext& ctx,
                                 const HCell& cell, int depth,
                                 const std::string& key) {
  const RFResult subset = filterByCell(result, cell);
  const bool ownContent = hasRenderable(subset, ctx.includeReceivers);

  json children = json::array();
  if (depth < ctx.maxDepth) {
    const std::array<HCell, 4> quads = quadrants(cell);
    for (int i = 0; i < 4; ++i) {
      std::optional<json> child = buildLodTile(
          result, ctx, quads[i], depth + 1, key + std::to_string(i));
      if (child) children.push_back(std::move(*child));
    }
  }

  if (!ownContent && children.empty()) return std::nullopt;

  json tile;
  tile["boundingVolume"] = {{"box", cellBox(cell, ctx.zlo, ctx.zhi)}};
  tile["geometricError"] = ctx.baseError / static_cast<double>(1 << depth);
  tile["refine"] = "REPLACE";
  if (ownContent) {
    const std::string name = "content_" + key + ".glb";
    writeGlbFile(subset, ctx.dir / name, ctx.includeReceivers);
    tile["content"] = {{"uri", name}};
  }
  if (!children.empty()) tile["children"] = std::move(children);
  return tile;
}
}  // namespace

void exportPaths3DTiles(const RFResult& result, const std::string& outputDir,
                        bool includeReceivers) {
  namespace fs = std::filesystem;
  fs::create_directories(outputDir);

  const std::string contentName = "content.glb";
  const fs::path glbPath = fs::path(outputDir) / contentName;
  const fs::path tilesetPath = fs::path(outputDir) / "tileset.json";

  // Reuse the glTF path exporter, then wrap its JSON in a GLB container.
  const std::vector<std::uint8_t> glb =
      gltfJsonToGlb(pathsToGltfString(result, includeReceivers));
  {
    std::ofstream out(glbPath, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write GLB to '" + glbPath.string() + "'");
    out.write(reinterpret_cast<const char*>(glb.data()),
              static_cast<std::streamsize>(glb.size()));
  }

  // 3D Tiles 1.1 bounding volume: box = [center(3), x-halfaxis(3),
  // y-halfaxis(3), z-halfaxis(3)].
  const Bounds b = computeBounds(result);
  const Vec3 center = 0.5 * (b.lo + b.hi);
  const Vec3 half = 0.5 * (b.hi - b.lo);
  const double diagonal = (b.hi - b.lo).norm();

  json box = json::array({center.x(), center.y(), center.z(),
                          half.x(), 0.0, 0.0,
                          0.0, half.y(), 0.0,
                          0.0, 0.0, half.z()});

  json tileset;
  tileset["asset"] = {{"version", "1.1"}, {"generator", "RFTraceKit"}};
  tileset["geometricError"] = diagonal;
  tileset["root"] = {
      {"boundingVolume", {{"box", std::move(box)}}},
      {"geometricError", 0.0},
      {"refine", "ADD"},
      {"content", {{"uri", contentName}}}};

  std::ofstream out(tilesetPath);
  if (!out)
    throw std::runtime_error("cannot write tileset to '" + tilesetPath.string() + "'");
  out << tileset.dump(2);
}

void exportPaths3DTilesLod(const RFResult& result, const std::string& outputDir,
                           int maxDepth, bool includeReceivers) {
  namespace fs = std::filesystem;
  fs::create_directories(outputDir);

  const Bounds b = computeBounds(result);
  const HCell rootCell{b.lo.x(), b.hi.x(), b.lo.y(), b.hi.y()};
  const double diagonal = (b.hi - b.lo).norm();

  LodContext ctx;
  ctx.dir = outputDir;
  ctx.zlo = b.lo.z();
  ctx.zhi = b.hi.z();
  // Non-zero base error so every level (including leaves) stays positive and
  // each parent strictly exceeds its children (baseError / 2^depth).
  ctx.baseError = diagonal > 0.0 ? diagonal : 1.0;
  ctx.maxDepth = std::max(1, maxDepth);
  ctx.includeReceivers = includeReceivers;

  std::optional<json> root =
      buildLodTile(result, ctx, rootCell, /*depth=*/0, /*key=*/"r");
  if (!root) {
    // Empty scene: emit a valid, content-free root over the (unit) extent.
    root = json{{"boundingVolume", {{"box", cellBox(rootCell, ctx.zlo, ctx.zhi)}}},
                {"geometricError", 0.0},
                {"refine", "REPLACE"}};
  }

  json tileset;
  tileset["asset"] = {{"version", "1.1"}, {"generator", "RFTraceKit"}};
  tileset["geometricError"] = ctx.baseError;
  tileset["root"] = std::move(*root);

  const fs::path tilesetPath = fs::path(outputDir) / "tileset.json";
  std::ofstream out(tilesetPath);
  if (!out)
    throw std::runtime_error("cannot write tileset to '" + tilesetPath.string() + "'");
  out << tileset.dump(2);
}

}  // namespace rftrace::io
