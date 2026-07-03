// rftrace-scene-validator — load a scene, print a summary, and report structural
// problems (empty scene, degenerate triangles, out-of-range material indices).
// Exits non-zero when the scene is invalid. Consumes only the public API.
#include <iostream>
#include <string>
#include <vector>

#include "cli_common.hpp"
#include "rftrace/importers/cityjson_importer.hpp"
#include "rftrace/importers/geojson_importer.hpp"
#include "rftrace/importers/osm_importer.hpp"
#include "rftrace/importers/osm_pbf_importer.hpp"
#include "rftrace/rftrace.hpp"

#ifndef RFTRACE_CLI_VERSION
#define RFTRACE_CLI_VERSION "unknown"
#endif

using namespace rftrace;
using namespace rftrace::cli;

namespace {

void printHelp() {
  std::cout <<
      "rftrace-scene-validator — validate a scene file\n"
      "\n"
      "Usage: rftrace-scene-validator [--scene] <path> [options]\n"
      "\n"
      "  --scene <path>          scene file (also accepted positionally)\n"
      "  --scene-format <fmt>    mesh|geojson|cityjson|osm|osmpbf\n"
      "  --materials <path.json> load material definitions first\n"
      "  -h, --help              show this help\n"
      "      --version           show version\n";
}

SceneFormat resolveSceneFormat(const ArgParser& args, const std::string& path) {
  if (args.has("scene-format"))
    return sceneFormatFromString(args.get("scene-format"));
  const SceneFormat fmt = sceneFormatFromPath(path);
  if (fmt == SceneFormat::Unknown)
    throw std::runtime_error("cannot infer scene format for '" + path +
                             "'; pass --scene-format");
  return fmt;
}

void loadSceneGeometry(Scene& scene, SceneFormat fmt, const std::string& path) {
  switch (fmt) {
    case SceneFormat::Mesh: io::importMesh(scene, path); break;
    case SceneFormat::GeoJson: io::importGeoJSON(scene, path); break;
    case SceneFormat::CityJson: io::importCityJSON(scene, path); break;
    case SceneFormat::Osm: io::importOSM(scene, path); break;
    case SceneFormat::OsmPbf:
      if (!io::osmiumAvailable())
        throw std::runtime_error(
            "OSM PBF input is not available (built without osmium)");
      io::importOSMPbf(scene, path);
      break;
    case SceneFormat::Unknown:
      throw std::runtime_error("cannot infer scene format for '" + path + "'");
  }
}

std::string scenePath(const ArgParser& args) {
  if (args.has("scene")) return args.get("scene");
  if (!args.positional().empty()) return args.positional().front();
  throw std::runtime_error("no scene given (pass a path or --scene <path>)");
}

void printSummary(const Scene& scene) {
  AABB bbox;
  for (const auto& t : scene.triangles()) bbox.expand(t);
  std::cout << "triangles: " << scene.triangles().size() << "\n";
  if (!bbox.empty()) {
    std::cout << "bbox: [" << bbox.lo.x() << ", " << bbox.lo.y() << ", "
              << bbox.lo.z() << "] .. [" << bbox.hi.x() << ", " << bbox.hi.y()
              << ", " << bbox.hi.z() << "]\n";
  }
  std::cout << "materials: " << scene.materials().size() << "\n";
  std::cout << "transmitters: " << scene.transmitters().size() << "\n";
  std::cout << "receivers: " << scene.receivers().size() << "\n";
}

/// Collect problem messages; empty vector => the scene is valid.
std::vector<std::string> findProblems(const Scene& scene) {
  std::vector<std::string> problems;
  const auto& tris = scene.triangles();
  if (tris.empty()) problems.push_back("empty scene (0 triangles)");

  int degenerate = 0;
  int badMaterial = 0;
  const int matCount = static_cast<int>(scene.materials().size());
  for (int i = 0; i < static_cast<int>(tris.size()); ++i) {
    if (tris[i].rawNormal().norm() == 0.0) ++degenerate;
    const int m = scene.triangleMaterialIndex(i);
    if (m < 0 || m >= matCount) ++badMaterial;
  }
  if (degenerate > 0)
    problems.push_back(std::to_string(degenerate) +
                       " degenerate / zero-area triangle(s)");
  if (badMaterial > 0)
    problems.push_back(std::to_string(badMaterial) +
                       " triangle(s) with out-of-range material index");
  return problems;
}

int validate(const ArgParser& args) {
  Scene scene;
  if (args.has("materials")) io::importMaterials(scene, args.get("materials"));
  const std::string path = scenePath(args);
  loadSceneGeometry(scene, resolveSceneFormat(args, path), path);

  printSummary(scene);

  const auto problems = findProblems(scene);
  if (problems.empty()) {
    std::cout << "scene OK\n";
    return 0;
  }
  for (const auto& p : problems) std::cerr << "problem: " << p << "\n";
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    ArgParser args(argc, argv);
    if (args.wantsHelp()) {
      printHelp();
      return 0;
    }
    if (args.wantsVersion()) {
      std::cout << "rftrace-scene-validator " << RFTRACE_CLI_VERSION << "\n";
      return 0;
    }
    return validate(args);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
