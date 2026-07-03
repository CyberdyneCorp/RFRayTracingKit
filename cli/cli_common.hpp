// cli_common.hpp — shared, header-only helpers for the RFTraceKit CLI tools.
//
// Dependency-free (C++ standard library only; may include public rftrace headers
// for enum types). It links NOTHING — every rftrace symbol it references is
// either an enum (header-only) or resolved by the tool's own link against
// rftrace::rftrace. Deliberately avoids nlohmann_json (which is PRIVATE-linked to
// the core), so the result-kind sniffer is a lightweight substring scan.
#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "rftrace/backend.hpp"
#include "rftrace/simulator.hpp"

namespace rftrace::cli {

// --- Small string utilities ---------------------------------------------------

inline std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

/// Split on a single-character delimiter, keeping empty fields.
inline std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> out;
  std::string field;
  std::istringstream ss(s);
  while (std::getline(ss, field, delim)) out.push_back(field);
  return out;
}

/// Trim ASCII whitespace from both ends.
inline std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

/// True when `s` ends with `suffix`.
inline bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Parse a comma-separated list of doubles (e.g. "0,0,10"). Throws on any
/// non-numeric field. `label` names the flag for the error message.
inline std::vector<double> parseDoubleList(const std::string& csv,
                                           const std::string& label) {
  std::vector<double> out;
  for (const auto& raw : split(csv, ',')) {
    const std::string tok = trim(raw);
    try {
      std::size_t pos = 0;
      const double v = std::stod(tok, &pos);
      if (pos != tok.size()) throw std::invalid_argument("trailing");
      out.push_back(v);
    } catch (const std::exception&) {
      throw std::runtime_error("invalid value for " + label + ": '" + tok +
                               "' (expected a number)");
    }
  }
  return out;
}

// --- ArgParser ----------------------------------------------------------------

/// Minimal, dependency-free argument parser. Grammar:
///   --key value        value-bearing option (value must not look like an option)
///   --key=value        value-bearing option
///   --flag             boolean flag (no value follows, or the next token is an
///                      option token)
///   -h                 alias for --help
///   --help / --version  recognized as flags
/// Repeatable keys are stored in a multimap; use getAll() to retrieve them all.
class ArgParser {
 public:
  ArgParser(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
      std::string tok = argv[i];
      if (tok == "-h" || tok == "--help") {
        flags_.insert("help");
        continue;
      }
      if (tok.rfind("--", 0) != 0) {
        positional_.push_back(tok);
        continue;
      }
      std::string key = tok.substr(2);
      const auto eq = key.find('=');
      if (eq != std::string::npos) {
        values_.emplace(key.substr(0, eq), key.substr(eq + 1));
        continue;
      }
      if (key == "version") {
        flags_.insert("version");
        continue;
      }
      // Lookahead: consume the next token as a value unless it is itself an
      // option token. A leading "--" or a bare "-h" is an option; a value like
      // "-10,-10" (a negative coordinate) is NOT.
      if (i + 1 < argc && !isOptionToken(argv[i + 1])) {
        values_.emplace(key, argv[++i]);
      } else {
        flags_.insert(key);
      }
    }
  }

  bool has(const std::string& key) const {
    return flags_.count(key) > 0 || values_.count(key) > 0;
  }

  bool wantsHelp() const { return flags_.count("help") > 0; }
  bool wantsVersion() const { return flags_.count("version") > 0; }

  const std::vector<std::string>& positional() const { return positional_; }

  /// All values supplied for a repeatable key, in command-line order.
  std::vector<std::string> getAll(const std::string& key) const {
    std::vector<std::string> out;
    auto range = values_.equal_range(key);
    for (auto it = range.first; it != range.second; ++it) out.push_back(it->second);
    return out;
  }

  /// Required string value; throws with a clear message when absent.
  std::string require(const std::string& key) const {
    auto it = values_.find(key);
    if (it == values_.end())
      throw std::runtime_error("missing required option --" + key);
    return it->second;
  }

  std::string get(const std::string& key) const { return require(key); }
  std::string get(const std::string& key, const std::string& def) const {
    auto it = values_.find(key);
    return it == values_.end() ? def : it->second;
  }

  int getInt(const std::string& key) const { return parseInt(require(key), key); }
  int getInt(const std::string& key, int def) const {
    auto it = values_.find(key);
    return it == values_.end() ? def : parseInt(it->second, key);
  }

  double getDouble(const std::string& key) const {
    return parseDouble(require(key), key);
  }
  double getDouble(const std::string& key, double def) const {
    auto it = values_.find(key);
    return it == values_.end() ? def : parseDouble(it->second, key);
  }

  /// A flag is true when present as a bare flag; a value-bearing form accepts
  /// true/false/1/0/on/off/yes/no. Missing => `def`.
  bool getBool(const std::string& key, bool def) const {
    if (flags_.count(key) > 0) return true;
    auto it = values_.find(key);
    if (it == values_.end()) return def;
    const std::string v = toLower(it->second);
    if (v == "true" || v == "1" || v == "on" || v == "yes") return true;
    if (v == "false" || v == "0" || v == "off" || v == "no") return false;
    throw std::runtime_error("invalid boolean for --" + key + ": '" + it->second +
                             "'");
  }

 private:
  static bool isOptionToken(const std::string& s) {
    return s.rfind("--", 0) == 0 || s == "-h";
  }

  static int parseInt(const std::string& v, const std::string& key) {
    try {
      std::size_t pos = 0;
      const int n = std::stoi(v, &pos);
      if (pos != v.size()) throw std::invalid_argument("trailing");
      return n;
    } catch (const std::exception&) {
      throw std::runtime_error("invalid value for --" + key + ": '" + v +
                               "' (expected an integer)");
    }
  }

  static double parseDouble(const std::string& v, const std::string& key) {
    try {
      std::size_t pos = 0;
      const double d = std::stod(v, &pos);
      if (pos != v.size()) throw std::invalid_argument("trailing");
      return d;
    } catch (const std::exception&) {
      throw std::runtime_error("invalid value for --" + key + ": '" + v +
                               "' (expected a number)");
    }
  }

  std::multimap<std::string, std::string> values_;
  std::set<std::string> flags_;
  std::vector<std::string> positional_;
};

// --- Output format dispatch (by file extension) -------------------------------

enum class OutFormat { Json, Csv, GeoJson, Gltf, GeoTiff, Parquet, Unknown };

inline std::string toString(OutFormat f) {
  switch (f) {
    case OutFormat::Json: return "json";
    case OutFormat::Csv: return "csv";
    case OutFormat::GeoJson: return "geojson";
    case OutFormat::Gltf: return "gltf";
    case OutFormat::GeoTiff: return "geotiff";
    case OutFormat::Parquet: return "parquet";
    case OutFormat::Unknown: return "unknown";
  }
  return "unknown";
}

/// Map an output path's extension to a format enum. `.geojson` is checked before
/// `.json` so it wins.
inline OutFormat formatFromPath(const std::string& path) {
  const std::string p = toLower(path);
  if (endsWith(p, ".geojson")) return OutFormat::GeoJson;
  if (endsWith(p, ".json")) return OutFormat::Json;
  if (endsWith(p, ".csv")) return OutFormat::Csv;
  if (endsWith(p, ".gltf") || endsWith(p, ".glb")) return OutFormat::Gltf;
  if (endsWith(p, ".tif") || endsWith(p, ".tiff")) return OutFormat::GeoTiff;
  if (endsWith(p, ".parquet")) return OutFormat::Parquet;
  return OutFormat::Unknown;
}

// --- Scene format detection ---------------------------------------------------

enum class SceneFormat { Mesh, GeoJson, CityJson, Osm, OsmPbf, Unknown };

/// Infer the scene input format from a path extension. `.json` alone is
/// ambiguous (could be CityJSON) and returns Unknown so the caller can require
/// an explicit --scene-format.
inline SceneFormat sceneFormatFromPath(const std::string& path) {
  const std::string p = toLower(path);
  if (endsWith(p, ".obj") || endsWith(p, ".gltf") || endsWith(p, ".glb"))
    return SceneFormat::Mesh;
  if (endsWith(p, ".geojson")) return SceneFormat::GeoJson;
  if (endsWith(p, ".city.json") || endsWith(p, ".cityjson"))
    return SceneFormat::CityJson;
  if (endsWith(p, ".osm.pbf") || endsWith(p, ".pbf")) return SceneFormat::OsmPbf;
  if (endsWith(p, ".osm") || endsWith(p, ".xml")) return SceneFormat::Osm;
  return SceneFormat::Unknown;
}

/// Parse a --scene-format override (mesh|geojson|cityjson|osm|osmpbf).
inline SceneFormat sceneFormatFromString(const std::string& name) {
  const std::string n = toLower(name);
  if (n == "mesh" || n == "obj" || n == "gltf") return SceneFormat::Mesh;
  if (n == "geojson") return SceneFormat::GeoJson;
  if (n == "cityjson" || n == "city") return SceneFormat::CityJson;
  if (n == "osm" || n == "osmxml") return SceneFormat::Osm;
  if (n == "osmpbf" || n == "pbf") return SceneFormat::OsmPbf;
  throw std::runtime_error("unknown --scene-format: '" + name +
                           "' (expected mesh|geojson|cityjson|osm|osmpbf)");
}

// --- Enum parsers lacking a public string parser ------------------------------

inline PropagationMode parseMode(const std::string& name) {
  const std::string n = toLower(name);
  if (n == "image" || n == "imagemethod") return PropagationMode::ImageMethod;
  if (n == "raylaunch" || n == "ray") return PropagationMode::RayLaunch;
  throw std::runtime_error("unknown --mode: '" + name +
                           "' (expected image|raylaunch)");
}

inline DiffractionModel parseDiffraction(const std::string& name) {
  const std::string n = toLower(name);
  if (n == "single" || n == "singleedge") return DiffractionModel::SingleEdge;
  if (n == "bullington") return DiffractionModel::Bullington;
  if (n == "deygout") return DiffractionModel::Deygout;
  if (n == "utd") return DiffractionModel::UTD;
  throw std::runtime_error("unknown --diffraction model: '" + name +
                           "' (expected single|bullington|deygout|utd)");
}

// --- Result-kind sniffing (substring scan; no JSON parser) --------------------

enum class ResultKind { Point, Coverage, Route, Unknown };

/// Classify an rftrace-cli JSON result by scanning for distinguishing keys.
/// A route carries "samples"; a coverage carries a "grid" block plus "power_dbm";
/// a point result carries "receivers".
inline ResultKind resultKindFromJsonText(const std::string& text) {
  if (text.find("\"samples\"") != std::string::npos) return ResultKind::Route;
  if (text.find("\"grid\"") != std::string::npos &&
      text.find("\"power_dbm\"") != std::string::npos)
    return ResultKind::Coverage;
  if (text.find("\"receivers\"") != std::string::npos) return ResultKind::Point;
  return ResultKind::Unknown;
}

}  // namespace rftrace::cli
