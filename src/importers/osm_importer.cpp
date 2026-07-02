#include "rftrace/importers/osm_importer.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "osm_extract.hpp"

namespace rftrace::io {

namespace {
using nlohmann::json;

// Node table + way representation + building/vegetation extraction are shared
// with the OSM PBF reader (detail::buildOsmScene). The Overpass-JSON and `.osm`
// XML readers below only differ in how they populate these structures.
using detail::LatLon;
using detail::RawWay;
using detail::Tags;

// --- Overpass JSON reader ----------------------------------------------------

std::size_t doImportJson(Scene& scene, const std::string& text,
                         const OsmImportOptions& opts) {
  json doc;
  try {
    doc = json::parse(text);
  } catch (const std::exception& e) {
    throw SceneError(std::string("invalid Overpass JSON: ") + e.what());
  }
  if (!doc.is_object() || !doc.contains("elements") ||
      !doc.at("elements").is_array())
    throw SceneError(
        "not a valid Overpass JSON document (need an 'elements' array)");

  const json& elements = doc.at("elements");

  std::unordered_map<std::int64_t, LatLon> nodes;
  for (const json& e : elements) {
    if (e.value("type", "") == "node" && e.contains("id") &&
        e.contains("lat") && e.contains("lon"))
      nodes[e.at("id").get<std::int64_t>()] = {e.at("lat").get<double>(),
                                               e.at("lon").get<double>()};
  }

  std::vector<RawWay> ways;
  for (const json& e : elements) {
    if (e.value("type", "") != "way" || !e.contains("nodes")) continue;
    RawWay way;
    for (const json& n : e.at("nodes")) way.refs.push_back(n.get<std::int64_t>());
    if (e.contains("tags") && e.at("tags").is_object()) {
      for (const auto& [k, v] : e.at("tags").items())
        if (v.is_string()) way.tags[k] = v.get<std::string>();
    }
    ways.push_back(std::move(way));
  }

  return detail::buildOsmScene(
      scene, nodes, ways, opts,
      "Overpass JSON contains no building or vegetation ways");
}

// --- OSM XML reader ----------------------------------------------------------
// A lightweight, dependency-free scanner over the small subset of OSM XML we
// need: <node id lat lon .../>, and <way> ... <nd ref/> ... <tag k v/> ...
// </way>. It is intentionally not a general XML parser; it recognises element
// names and quoted attributes and ignores everything else (comments, the XML
// declaration, <bounds>, <relation>, ...). Structural corruption (an
// unterminated tag) is reported as a SceneError.

/// Parse `name="value"` / `name='value'` attributes out of a tag's interior.
Tags parseAttributes(const std::string& body) {
  Tags attrs;
  std::size_t i = 0;
  const std::size_t n = body.size();
  while (i < n) {
    while (i < n && (std::isspace(static_cast<unsigned char>(body[i])) != 0))
      ++i;
    const std::size_t nameStart = i;
    while (i < n && body[i] != '=' &&
           std::isspace(static_cast<unsigned char>(body[i])) == 0)
      ++i;
    if (i >= n || nameStart == i) break;
    const std::string name = body.substr(nameStart, i - nameStart);
    while (i < n && std::isspace(static_cast<unsigned char>(body[i])) != 0) ++i;
    if (i >= n || body[i] != '=') continue;  // attribute without a value
    ++i;                                     // consume '='
    while (i < n && std::isspace(static_cast<unsigned char>(body[i])) != 0) ++i;
    if (i >= n || (body[i] != '"' && body[i] != '\'')) break;
    const char quote = body[i++];
    const std::size_t valStart = i;
    while (i < n && body[i] != quote) ++i;
    if (i >= n) break;  // unterminated attribute value; ignore the remainder
    attrs[name] = body.substr(valStart, i - valStart);
    ++i;  // consume the closing quote
  }
  return attrs;
}

std::size_t doImportXml(Scene& scene, const std::string& text,
                        const OsmImportOptions& opts) {
  std::unordered_map<std::int64_t, LatLon> nodes;
  std::vector<RawWay> ways;
  RawWay current;
  bool inWay = false;

  std::size_t pos = 0;
  const std::size_t n = text.size();
  while (pos < n) {
    const std::size_t lt = text.find('<', pos);
    if (lt == std::string::npos) break;
    // Scan to the tag's closing '>', ignoring any '>' inside a quoted attribute
    // value (e.g. <tag v="a>b"/>) so the element is not truncated mid-attribute.
    std::size_t gt = std::string::npos;
    char quote = 0;
    for (std::size_t i = lt + 1; i < n; ++i) {
      const char c = text[i];
      if (quote != 0) {
        if (c == quote) quote = 0;
      } else if (c == '"' || c == '\'') {
        quote = c;
      } else if (c == '>') {
        gt = i;
        break;
      }
    }
    if (gt == std::string::npos)
      throw SceneError("malformed OSM XML: unterminated tag");
    std::string tag = text.substr(lt + 1, gt - lt - 1);
    pos = gt + 1;
    if (tag.empty()) continue;
    // Skip the XML declaration <?xml?>, comments/DOCTYPE <!...>.
    if (tag.front() == '?' || tag.front() == '!') continue;

    const bool closing = tag.front() == '/';
    if (closing) tag.erase(tag.begin());
    bool selfClosing = false;
    if (!tag.empty() && tag.back() == '/') {
      selfClosing = true;
      tag.pop_back();
    }

    // Split element name from its attribute body.
    std::size_t sp = 0;
    while (sp < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[sp])) == 0)
      ++sp;
    const std::string name = tag.substr(0, sp);
    const std::string body = sp < tag.size() ? tag.substr(sp) : std::string{};

    if (closing) {
      if (name == "way" && inWay) {
        ways.push_back(std::move(current));
        current = RawWay{};
        inWay = false;
      }
      continue;
    }

    if (name == "node") {
      const Tags a = parseAttributes(body);
      auto id = a.find("id");
      auto lat = a.find("lat");
      auto lon = a.find("lon");
      if (id != a.end() && lat != a.end() && lon != a.end()) {
        try {
          nodes[std::stoll(id->second)] = {std::stod(lat->second),
                                           std::stod(lon->second)};
        } catch (const std::exception&) {
        }
      }
    } else if (name == "way") {
      current = RawWay{};
      inWay = true;
      if (selfClosing) {  // empty <way/> — nothing to collect
        ways.push_back(std::move(current));
        current = RawWay{};
        inWay = false;
      }
    } else if (name == "nd" && inWay) {
      const Tags a = parseAttributes(body);
      auto ref = a.find("ref");
      if (ref != a.end()) {
        try {
          current.refs.push_back(std::stoll(ref->second));
        } catch (const std::exception&) {
        }
      }
    } else if (name == "tag" && inWay) {
      const Tags a = parseAttributes(body);
      auto k = a.find("k");
      auto v = a.find("v");
      if (k != a.end() && v != a.end()) current.tags[k->second] = v->second;
    }
  }

  return detail::buildOsmScene(scene, nodes, ways, opts,
                               "OSM XML contains no building or vegetation ways");
}

/// Peek at the first non-whitespace byte to route between the XML ('<') and
/// Overpass-JSON ('{') readers.
char firstNonSpace(const std::string& text) {
  for (char c : text)
    if (std::isspace(static_cast<unsigned char>(c)) == 0) return c;
  return '\0';
}

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw SceneError("cannot open OSM file '" + path + "'");
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
}  // namespace

std::size_t importOSM(Scene& scene, const std::string& path,
                      const OsmImportOptions& opts) {
  const std::string text = readFile(path);
  if (firstNonSpace(text) == '<') return doImportXml(scene, text, opts);
  return doImportJson(scene, text, opts);
}

std::size_t importOSMXml(Scene& scene, const std::string& path,
                         const OsmImportOptions& opts) {
  return doImportXml(scene, readFile(path), opts);
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

std::size_t Scene::loadOSMXml(const std::string& path,
                              const std::string& buildingMaterial,
                              const std::string& vegetationMaterial) {
  io::OsmImportOptions opts;
  opts.buildingMaterial = buildingMaterial;
  opts.vegetationMaterial = vegetationMaterial;
  return io::importOSMXml(*this, path, opts);
}
}  // namespace rftrace
