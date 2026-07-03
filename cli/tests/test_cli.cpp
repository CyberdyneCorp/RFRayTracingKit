// Tests for the RFTraceKit CLI tools.
//   UNIT — exercise cli_common.hpp directly (ArgParser, format detection, enum
//          parsers, result-kind sniffer).
//   INTEGRATION — invoke the built binaries via std::system on tiny fixtures
//          written into testing::TempDir(), asserting exit codes and output.
#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "cli_common.hpp"

using namespace rftrace::cli;

// --- Unit: ArgParser ----------------------------------------------------------

namespace {
ArgParser make(std::vector<const char*> argv) {
  return ArgParser(static_cast<int>(argv.size()),
                   const_cast<char**>(argv.data()));
}
}  // namespace

TEST(ArgParser, KeyValueSpaceAndEquals) {
  auto a = make({"prog", "--out", "r.json", "--mode=image"});
  EXPECT_EQ(a.get("out"), "r.json");
  EXPECT_EQ(a.get("mode"), "image");
}

TEST(ArgParser, BareFlagAndHelpAlias) {
  auto a = make({"prog", "--verbose", "-h"});
  EXPECT_TRUE(a.has("verbose"));
  EXPECT_TRUE(a.wantsHelp());
}

TEST(ArgParser, VersionFlag) {
  auto a = make({"prog", "--version"});
  EXPECT_TRUE(a.wantsVersion());
}

TEST(ArgParser, RepeatableGetAll) {
  auto a = make({"prog", "--tx", "0,0,10", "--tx", "5,5,5"});
  auto all = a.getAll("tx");
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0], "0,0,10");
  EXPECT_EQ(all[1], "5,5,5");
}

TEST(ArgParser, NegativeNumberIsAValueNotAFlag) {
  auto a = make({"prog", "--grid", "-10,-10,5,4,4,1.5"});
  EXPECT_EQ(a.get("grid"), "-10,-10,5,4,4,1.5");
}

TEST(ArgParser, TypedGettersHappyPath) {
  auto a = make({"prog", "--n", "42", "--f", "3.5"});
  EXPECT_EQ(a.getInt("n"), 42);
  EXPECT_DOUBLE_EQ(a.getDouble("f"), 3.5);
}

TEST(ArgParser, IntThrowsOnNonNumeric) {
  auto a = make({"prog", "--n", "abc"});
  EXPECT_THROW(a.getInt("n"), std::runtime_error);
}

TEST(ArgParser, IntThrowsOnTrailingGarbage) {
  auto a = make({"prog", "--n", "42x"});
  EXPECT_THROW(a.getInt("n"), std::runtime_error);
}

TEST(ArgParser, DoubleThrowsOnTrailingGarbage) {
  auto a = make({"prog", "--f", "3.5q"});
  EXPECT_THROW(a.getDouble("f"), std::runtime_error);
}

TEST(ArgParser, RequireThrowsWhenMissing) {
  auto a = make({"prog"});
  EXPECT_THROW(a.get("out"), std::runtime_error);
  EXPECT_EQ(a.get("out", "def"), "def");
}

TEST(ArgParser, Positional) {
  auto a = make({"prog", "scene.obj"});
  ASSERT_EQ(a.positional().size(), 1u);
  EXPECT_EQ(a.positional().front(), "scene.obj");
}

// --- Unit: formatFromPath -----------------------------------------------------

TEST(FormatFromPath, EachExtension) {
  EXPECT_EQ(formatFromPath("a.json"), OutFormat::Json);
  EXPECT_EQ(formatFromPath("a.csv"), OutFormat::Csv);
  EXPECT_EQ(formatFromPath("a.geojson"), OutFormat::GeoJson);
  EXPECT_EQ(formatFromPath("a.gltf"), OutFormat::Gltf);
  EXPECT_EQ(formatFromPath("a.glb"), OutFormat::Gltf);
  EXPECT_EQ(formatFromPath("a.tif"), OutFormat::GeoTiff);
  EXPECT_EQ(formatFromPath("a.tiff"), OutFormat::GeoTiff);
  EXPECT_EQ(formatFromPath("a.parquet"), OutFormat::Parquet);
  EXPECT_EQ(formatFromPath("a.xyz"), OutFormat::Unknown);
}

TEST(FormatFromPath, GeoJsonWinsOverJson) {
  EXPECT_EQ(formatFromPath("cov.geojson"), OutFormat::GeoJson);
}

// --- Unit: sceneFormat --------------------------------------------------------

TEST(SceneFormat, FromPath) {
  EXPECT_EQ(sceneFormatFromPath("m.obj"), SceneFormat::Mesh);
  EXPECT_EQ(sceneFormatFromPath("m.gltf"), SceneFormat::Mesh);
  EXPECT_EQ(sceneFormatFromPath("b.geojson"), SceneFormat::GeoJson);
  EXPECT_EQ(sceneFormatFromPath("c.city.json"), SceneFormat::CityJson);
  EXPECT_EQ(sceneFormatFromPath("d.osm"), SceneFormat::Osm);
  EXPECT_EQ(sceneFormatFromPath("d.osm.pbf"), SceneFormat::OsmPbf);
  EXPECT_EQ(sceneFormatFromPath("ambiguous.json"), SceneFormat::Unknown);
}

TEST(SceneFormat, Override) {
  EXPECT_EQ(sceneFormatFromString("mesh"), SceneFormat::Mesh);
  EXPECT_EQ(sceneFormatFromString("geojson"), SceneFormat::GeoJson);
  EXPECT_EQ(sceneFormatFromString("cityjson"), SceneFormat::CityJson);
  EXPECT_EQ(sceneFormatFromString("osm"), SceneFormat::Osm);
  EXPECT_EQ(sceneFormatFromString("osmpbf"), SceneFormat::OsmPbf);
  EXPECT_THROW(sceneFormatFromString("nope"), std::runtime_error);
}

// --- Unit: enum parsers -------------------------------------------------------

TEST(EnumParsers, Mode) {
  EXPECT_EQ(parseMode("image"), rftrace::PropagationMode::ImageMethod);
  EXPECT_EQ(parseMode("raylaunch"), rftrace::PropagationMode::RayLaunch);
  EXPECT_THROW(parseMode("bogus"), std::runtime_error);
}

TEST(EnumParsers, Diffraction) {
  EXPECT_EQ(parseDiffraction("single"), rftrace::DiffractionModel::SingleEdge);
  EXPECT_EQ(parseDiffraction("bullington"), rftrace::DiffractionModel::Bullington);
  EXPECT_EQ(parseDiffraction("deygout"), rftrace::DiffractionModel::Deygout);
  EXPECT_EQ(parseDiffraction("utd"), rftrace::DiffractionModel::UTD);
  EXPECT_THROW(parseDiffraction("bogus"), std::runtime_error);
}

// --- Unit: result-kind sniffer ------------------------------------------------

TEST(ResultKind, Sniff) {
  EXPECT_EQ(resultKindFromJsonText("{\"receivers\":[]}"), ResultKind::Point);
  EXPECT_EQ(resultKindFromJsonText("{\"grid\":{},\"power_dbm\":[]}"),
            ResultKind::Coverage);
  EXPECT_EQ(resultKindFromJsonText("{\"samples\":[]}"), ResultKind::Route);
  EXPECT_EQ(resultKindFromJsonText("{\"nope\":1}"), ResultKind::Unknown);
}

// ============================================================================
// Integration: invoke the built binaries.
// ============================================================================

namespace {

std::atomic<int> g_counter{0};

std::string tmp(const std::string& name) {
  return std::string(testing::TempDir()) + "cli_" +
         std::to_string(g_counter.fetch_add(1)) + "_" + name;
}

void writeFile(const std::string& path, const std::string& content) {
  std::ofstream out(path);
  out << content;
}

std::string readFile(const std::string& path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool fileExists(const std::string& path) {
  std::ifstream in(path);
  return in.good();
}

// Run a command, capturing merged stdout+stderr into `output`; returns the
// process exit code.
int run(const std::string& cmd, std::string& output) {
  const std::string logf = tmp("run.log");
  const int rc = std::system((cmd + " > \"" + logf + "\" 2>&1").c_str());
  output = readFile(logf);
#ifdef _WIN32
  return rc;
#else
  return WEXITSTATUS(rc);
#endif
}

// A flat quad (two non-degenerate triangles) on z=0.
const char* kTriObj =
    "v 0 0 0\nv 10 0 0\nv 10 10 0\nv 0 10 0\nf 1 2 3\nf 1 3 4\n";
// Three collinear vertices — a single zero-area triangle.
const char* kDegenObj = "v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n";

std::string q(const std::string& s) { return "\"" + s + "\""; }

}  // namespace

TEST(CliIntegration, VersionAndHelp) {
  std::string out;
  EXPECT_EQ(run(std::string(RFTRACE_CLI_BIN) + " --version", out), 0);
  EXPECT_NE(out.find("rftrace-cli"), std::string::npos);
  EXPECT_EQ(run(std::string(RFTRACE_CLI_BIN) + " --help", out), 0);
  EXPECT_NE(out.find("Usage"), std::string::npos);
}

TEST(CliIntegration, PointToJson) {
  const std::string out = tmp("r.json");
  std::string log;
  const int rc = run(std::string(RFTRACE_CLI_BIN) +
                         " --tx 0,0,10 --rx 50,0,1.5 --out " + q(out),
                     log);
  EXPECT_EQ(rc, 0) << log;
  ASSERT_TRUE(fileExists(out));
  EXPECT_NE(readFile(out).find("\"receivers\""), std::string::npos);
}

TEST(CliIntegration, PointToCsvViaReceiversFile) {
  const std::string rxFile = tmp("rx.csv");
  writeFile(rxFile, "# id,x,y,z\nrx_a,50,0,1.5\nrx_b,80,10,1.5\n");
  const std::string out = tmp("r.csv");
  std::string log;
  const int rc = run(std::string(RFTRACE_CLI_BIN) + " --tx 0,0,10 --receivers " +
                         q(rxFile) + " --out " + q(out),
                     log);
  EXPECT_EQ(rc, 0) << log;
  ASSERT_TRUE(fileExists(out));
  EXPECT_NE(readFile(out).find("receiver_id"), std::string::npos);
}

TEST(CliIntegration, CoverageToCsv) {
  const std::string out = tmp("c.csv");
  std::string log;
  const int rc = run(std::string(RFTRACE_CLI_BIN) +
                         " --tx 0,0,10 --grid -10,-10,5,4,4,1.5 --out " + q(out),
                     log);
  EXPECT_EQ(rc, 0) << log;
  ASSERT_TRUE(fileExists(out));
  EXPECT_NE(readFile(out).find("row,col"), std::string::npos);
}

TEST(CliIntegration, MeshSceneLoads) {
  const std::string obj = tmp("tri.obj");
  writeFile(obj, kTriObj);
  const std::string out = tmp("r.json");
  std::string log;
  const int rc = run(std::string(RFTRACE_CLI_BIN) + " --scene " + q(obj) +
                         " --tx 5,5,20 --rx 5,5,1 --out " + q(out),
                     log);
  EXPECT_EQ(rc, 0) << log;
  EXPECT_TRUE(fileExists(out));
}

TEST(CliIntegration, MissingSceneFails) {
  std::string log;
  const int rc = run(std::string(RFTRACE_CLI_BIN) +
                         " --scene /no/such/file.obj --tx 0,0,10 --rx 5,0,1 --out " +
                         q(tmp("r.json")),
                     log);
  EXPECT_NE(rc, 0);
  EXPECT_NE(log.find("error:"), std::string::npos);
}

TEST(CliIntegration, UnknownOutputFormatFails) {
  std::string log;
  const int rc = run(
      std::string(RFTRACE_CLI_BIN) + " --tx 0,0,10 --rx 5,0,1 --out r.xyz", log);
  EXPECT_NE(rc, 0);
  EXPECT_NE(log.find("error:"), std::string::npos);
}

TEST(CliIntegration, GeoTiffAvailability) {
  const std::string out = tmp("c.tif");
  std::string log;
  const int rc = run(std::string(RFTRACE_CLI_BIN) +
                         " --tx 0,0,10 --grid -10,-10,5,4,4,1.5 --out " + q(out),
                     log);
#ifdef RFTRACE_HAVE_GDAL
  EXPECT_EQ(rc, 0) << log;
  EXPECT_TRUE(fileExists(out));
#else
  EXPECT_NE(rc, 0);
  EXPECT_NE(log.find("not available"), std::string::npos);
  EXPECT_FALSE(fileExists(out));  // no partial file
#endif
}

TEST(CliIntegration, ParquetAvailability) {
  const std::string out = tmp("r.parquet");
  std::string log;
  const int rc = run(std::string(RFTRACE_CLI_BIN) +
                         " --tx 0,0,10 --rx 50,0,1.5 --out " + q(out),
                     log);
#ifdef RFTRACE_HAVE_PARQUET
  EXPECT_EQ(rc, 0) << log;
  EXPECT_TRUE(fileExists(out));
#else
  EXPECT_NE(rc, 0);
  EXPECT_NE(log.find("not available"), std::string::npos);
  EXPECT_FALSE(fileExists(out));
#endif
}

// --- Validator ----------------------------------------------------------------

TEST(ValidatorIntegration, ValidScene) {
  const std::string obj = tmp("tri.obj");
  writeFile(obj, kTriObj);
  std::string log;
  const int rc = run(std::string(RFTRACE_VALIDATOR_BIN) + " " + q(obj), log);
  EXPECT_EQ(rc, 0) << log;
  EXPECT_NE(log.find("triangles"), std::string::npos);
  EXPECT_NE(log.find("scene OK"), std::string::npos);
}

TEST(ValidatorIntegration, DegenerateSceneFails) {
  const std::string obj = tmp("degen.obj");
  writeFile(obj, kDegenObj);
  std::string log;
  const int rc = run(std::string(RFTRACE_VALIDATOR_BIN) + " " + q(obj), log);
  EXPECT_NE(rc, 0) << log;
  EXPECT_NE(log.find("degenerate"), std::string::npos);
}

TEST(ValidatorIntegration, MissingSceneFails) {
  std::string log;
  const int rc = run(std::string(RFTRACE_VALIDATOR_BIN), log);
  EXPECT_NE(rc, 0);
  EXPECT_NE(log.find("error:"), std::string::npos);
}

// --- Converter ----------------------------------------------------------------

TEST(ConverterIntegration, JsonToCsvAndGeoJson) {
  // First produce a point result via rftrace-cli.
  const std::string rj = tmp("r.json");
  std::string log;
  ASSERT_EQ(run(std::string(RFTRACE_CLI_BIN) +
                    " --tx 0,0,10 --rx 50,0,1.5 --out " + q(rj),
                log),
            0)
      << log;

  const std::string csv = tmp("r.csv");
  EXPECT_EQ(run(std::string(RFTRACE_CONVERTER_BIN) + " --in " + q(rj) + " --out " +
                    q(csv),
                log),
            0)
      << log;
  ASSERT_TRUE(fileExists(csv));
  EXPECT_NE(readFile(csv).find("receiver_id"), std::string::npos);

  const std::string geo = tmp("r.geojson");
  EXPECT_EQ(run(std::string(RFTRACE_CONVERTER_BIN) + " --in " + q(rj) + " --out " +
                    q(geo),
                log),
            0)
      << log;
  EXPECT_TRUE(fileExists(geo));
}

TEST(ConverterIntegration, UnsupportedOutputFails) {
  const std::string rj = tmp("r.json");
  std::string log;
  ASSERT_EQ(run(std::string(RFTRACE_CLI_BIN) +
                    " --tx 0,0,10 --rx 50,0,1.5 --out " + q(rj),
                log),
            0)
      << log;
  const int rc = run(
      std::string(RFTRACE_CONVERTER_BIN) + " --in " + q(rj) + " --out r.tif", log);
  EXPECT_NE(rc, 0);
  EXPECT_NE(log.find("error:"), std::string::npos);
}
