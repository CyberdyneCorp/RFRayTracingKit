// rftrace-cli — load a scene, run an RF propagation simulation, and export the
// result. Consumes only the public rftrace API; links rftrace::rftrace.
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cli_common.hpp"
#include "rftrace/exporters/csv_exporter.hpp"
#include "rftrace/exporters/geojson_exporter.hpp"
#include "rftrace/exporters/geotiff_heatmap.hpp"
#include "rftrace/exporters/gltf_exporter.hpp"
#include "rftrace/exporters/json_exporter.hpp"
#include "rftrace/exporters/parquet_exporter.hpp"
#include "rftrace/importers/cityjson_importer.hpp"
#include "rftrace/importers/geojson_importer.hpp"
#include "rftrace/importers/geotiff_terrain.hpp"
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
      "rftrace-cli — RF ray-tracing propagation from the command line\n"
      "\n"
      "Usage: rftrace-cli [options]\n"
      "\n"
      "Scene:\n"
      "  --scene <path>            scene file (.obj/.gltf/.glb mesh, .geojson,\n"
      "                            .city.json, .osm/.xml, .osm.pbf). Optional:\n"
      "                            an empty scene runs LOS-only.\n"
      "  --scene-format <fmt>      override detection: mesh|geojson|cityjson|osm|osmpbf\n"
      "  --materials <path.json>   load material definitions\n"
      "  --terrain <path.tif>      load a GeoTIFF DEM (needs GDAL)\n"
      "\n"
      "Sources / sinks:\n"
      "  --tx x,y,z[,freq,power]   add a transmitter (repeatable)\n"
      "  --rx x,y,z                add a receiver (repeatable)\n"
      "  --receivers <file>        receivers file: 'x,y,z' or 'id,x,y,z' per line\n"
      "\n"
      "Run mode (choose one; default is point receivers):\n"
      "  --grid ox,oy,cell,cols,rows,height   coverage grid\n"
      "  --route \"x,y,z;x,y,z;...\"            drive-test route\n"
      "  --route-spacing <m>                  route sample spacing (default 1.0)\n"
      "\n"
      "Settings:\n"
      "  --backend <name>          cpu|embree|metal|cuda|opencl (default cpu)\n"
      "  --mode image|raylaunch    propagation method (default image)\n"
      "  --freq <hz>               default transmitter frequency (default 3.5e9)\n"
      "  --max-reflections <n>     specular bounces (default 1)\n"
      "  --rays <n>                rays per transmitter (raylaunch)\n"
      "  --threads <n>             worker threads (0 = auto)\n"
      "  --seed <n>                RNG seed\n"
      "  --capture-radius <m>      receiver capture radius (raylaunch)\n"
      "  --diffraction [model]     enable diffraction (single|bullington|deygout|utd)\n"
      "  --sim-id <name>           simulation id\n"
      "\n"
      "Output:\n"
      "  --out <path>              write result; format from extension\n"
      "                            (.json/.csv/.geojson/.gltf/.tif/.parquet)\n"
      "\n"
      "  -h, --help                show this help\n"
      "      --version             show version\n";
}

// --- Scene loading ------------------------------------------------------------

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

void loadScene(Scene& scene, const ArgParser& args) {
  if (args.has("materials")) io::importMaterials(scene, args.get("materials"));
  if (args.has("scene")) {
    const std::string path = args.get("scene");
    loadSceneGeometry(scene, resolveSceneFormat(args, path), path);
  }
  if (args.has("terrain")) {
    if (!io::gdalAvailable())
      throw std::runtime_error("--terrain requires GDAL (built without GDAL)");
    scene.loadTerrain(args.get("terrain"));
  }
}

// --- Transmitters / receivers -------------------------------------------------

void addTransmitters(Scene& scene, const ArgParser& args, double defaultFreq) {
  int n = 0;
  for (const auto& spec : args.getAll("tx")) {
    const auto v = parseDoubleList(spec, "--tx");
    if (v.size() < 3)
      throw std::runtime_error("--tx needs at least x,y,z (got '" + spec + "')");
    Transmitter tx;
    tx.id = "tx" + std::to_string(n++);
    tx.position = {v[0], v[1], v[2]};
    tx.frequencyHz = v.size() > 3 ? v[3] : defaultFreq;
    tx.powerDbm = v.size() > 4 ? v[4] : 43.0;
    scene.addTransmitter(tx);
  }
}

void addReceiver(Scene& scene, const Vec3& pos, const std::string& id, int& n) {
  Receiver rx;
  rx.id = id.empty() ? "rx" + std::to_string(n) : id;
  rx.position = pos;
  scene.addReceiver(rx);
  ++n;
}

void addReceiversFromFile(Scene& scene, const std::string& path, int& n) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open receivers file '" + path + "'");
  std::string line;
  while (std::getline(in, line)) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    const auto fields = split(t, ',');
    if (fields.size() == 3) {
      const auto v = parseDoubleList(t, "receivers file");
      addReceiver(scene, {v[0], v[1], v[2]}, "", n);
    } else if (fields.size() == 4) {
      const std::string id = trim(fields[0]);
      const auto v = parseDoubleList(trim(fields[1]) + "," + trim(fields[2]) + "," +
                                         trim(fields[3]),
                                     "receivers file");
      addReceiver(scene, {v[0], v[1], v[2]}, id, n);
    } else {
      throw std::runtime_error("bad receivers line (expected x,y,z or id,x,y,z): '" +
                               t + "'");
    }
  }
}

void addReceivers(Scene& scene, const ArgParser& args) {
  int n = 0;
  for (const auto& spec : args.getAll("rx")) {
    const auto v = parseDoubleList(spec, "--rx");
    if (v.size() < 3)
      throw std::runtime_error("--rx needs x,y,z (got '" + spec + "')");
    addReceiver(scene, {v[0], v[1], v[2]}, "", n);
  }
  if (args.has("receivers")) addReceiversFromFile(scene, args.get("receivers"), n);
}

// --- Settings / run specs -----------------------------------------------------

SimulationSettings buildSettings(const ArgParser& args) {
  SimulationSettings s;
  if (args.has("backend")) s.backend = backendFromString(args.get("backend"));
  if (args.has("mode")) s.mode = parseMode(args.get("mode"));
  s.maxReflections = args.getInt("max-reflections", s.maxReflections);
  s.raysPerTransmitter = args.getInt("rays", s.raysPerTransmitter);
  s.threadCount = args.getInt("threads", s.threadCount);
  if (args.has("seed"))
    s.seed = static_cast<std::uint64_t>(args.getInt("seed"));
  s.captureRadius = args.getDouble("capture-radius", s.captureRadius);
  s.simulationId = args.get("sim-id", s.simulationId);
  if (args.has("diffraction")) {
    s.enableDiffraction = true;
    const auto vals = args.getAll("diffraction");
    s.diffractionModel = vals.empty() ? DiffractionModel::SingleEdge
                                      : parseDiffraction(vals.back());
  }
  return s;
}

CoverageGrid parseGrid(const std::string& spec) {
  const auto v = parseDoubleList(spec, "--grid");
  if (v.size() != 6)
    throw std::runtime_error(
        "--grid needs ox,oy,cell,cols,rows,height (got '" + spec + "')");
  CoverageGrid g;
  g.origin = {v[0], v[1], 0.0};
  g.cellSize = v[2];
  g.cols = static_cast<int>(v[3]);
  g.rows = static_cast<int>(v[4]);
  g.height = v[5];
  return g;
}

Route parseRoute(const ArgParser& args) {
  Route route;
  for (const auto& wp : split(args.get("route"), ';')) {
    const std::string t = trim(wp);
    if (t.empty()) continue;
    const auto v = parseDoubleList(t, "--route");
    if (v.size() < 3)
      throw std::runtime_error("--route waypoint needs x,y,z (got '" + t + "')");
    route.waypoints.push_back({v[0], v[1], v[2]});
  }
  if (route.waypoints.size() < 2)
    throw std::runtime_error("--route needs at least two waypoints");
  route.sampleSpacing = args.getDouble("route-spacing", 1.0);
  return route;
}

// --- Export dispatch ----------------------------------------------------------

void requireAvailable(bool ok, const std::string& fmt, const std::string& feature) {
  if (!ok)
    throw std::runtime_error(fmt + " output is not available (built without " +
                             feature + ")");
}

void exportPoint(const RFResult& result, const std::string& path, OutFormat fmt) {
  switch (fmt) {
    case OutFormat::Json: io::exportResultJson(result, path); break;
    case OutFormat::Csv: io::exportReceiversCsv(result, path); break;
    case OutFormat::GeoJson: io::exportReceiversGeoJson(result, path); break;
    case OutFormat::Gltf: io::exportPathsGltf(result, path); break;
    case OutFormat::Parquet:
      requireAvailable(io::parquetAvailable(), "Parquet", "Arrow/Parquet");
      io::exportReceiversParquet(result, path);
      break;
    case OutFormat::GeoTiff:
      throw std::runtime_error("GeoTIFF is a coverage-only format");
    case OutFormat::Unknown:
      throw std::runtime_error("unknown output format for '" + path + "'");
  }
}

void exportCoverage(const Scene& scene, const CoverageResult& result,
                    const std::string& path, OutFormat fmt) {
  switch (fmt) {
    case OutFormat::Json: io::exportCoverageJson(result, path); break;
    case OutFormat::Csv: io::exportCoverageCsv(result, path); break;
    case OutFormat::GeoJson: io::exportCoverageGeoJson(result, path); break;
    case OutFormat::GeoTiff:
      requireAvailable(io::gdalAvailable(), "GeoTIFF", "GDAL");
      io::exportCoverageGeoTiff(scene, result, path);
      break;
    case OutFormat::Gltf:
    case OutFormat::Parquet:
    case OutFormat::Unknown:
      throw std::runtime_error(
          "coverage supports only json/csv/geojson/geotiff (got '" + path + "')");
  }
}

void exportRoute(const RouteResult& result, const std::string& path,
                 OutFormat fmt) {
  switch (fmt) {
    case OutFormat::Json: io::exportRouteJson(result, path); break;
    case OutFormat::Csv: io::exportRouteCsv(result, path); break;
    default:
      throw std::runtime_error("route supports only json/csv (got '" + path +
                               "')");
  }
}

// --- Availability preflight (no partial file on unavailable feature) ----------

void preflightOutput(OutFormat fmt) {
  if (fmt == OutFormat::GeoTiff)
    requireAvailable(io::gdalAvailable(), "GeoTIFF", "GDAL");
  if (fmt == OutFormat::Parquet)
    requireAvailable(io::parquetAvailable(), "Parquet", "Arrow/Parquet");
  if (fmt == OutFormat::Unknown)
    throw std::runtime_error("unknown output format (use a known extension)");
}

int run(const ArgParser& args) {
  Scene scene;
  loadScene(scene, args);

  const double defaultFreq = args.getDouble("freq", 3.5e9);
  addTransmitters(scene, args, defaultFreq);
  addReceivers(scene, args);

  const SimulationSettings settings = buildSettings(args);
  Simulator sim(settings);

  const bool wantOut = args.has("out");
  const std::string outPath = wantOut ? args.get("out") : "";
  const OutFormat fmt = wantOut ? formatFromPath(outPath) : OutFormat::Unknown;
  if (wantOut) preflightOutput(fmt);

  if (args.has("grid")) {
    const CoverageResult result = sim.runCoverage(scene, parseGrid(args.get("grid")));
    std::cout << "coverage: " << result.grid.cellCount() << " cells\n";
    if (wantOut) exportCoverage(scene, result, outPath, fmt);
  } else if (args.has("route")) {
    const RouteResult result = sim.runRoute(scene, parseRoute(args));
    std::cout << "route: " << result.samples.size() << " samples\n";
    if (wantOut) exportRoute(result, outPath, fmt);
  } else {
    const RFResult result = sim.run(scene);
    std::cout << "point: " << result.receivers.size() << " receivers\n";
    if (wantOut) exportPoint(result, outPath, fmt);
  }
  if (wantOut) std::cout << "wrote " << outPath << "\n";
  return 0;
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
      std::cout << "rftrace-cli " << RFTRACE_CLI_VERSION << "\n";
      return 0;
    }
    return run(args);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
