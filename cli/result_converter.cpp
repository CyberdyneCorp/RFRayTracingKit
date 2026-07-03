// rftrace-result-converter — read an rftrace-cli JSON result and convert it to
// another supported format (JSON->CSV, JSON->GeoJSON, JSON->glTF). Point results
// only: there is no public loader for coverage/route JSON, so those input kinds
// report a clear unsupported-conversion error. Consumes only the public API.
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "cli_common.hpp"
#include "rftrace/exporters/csv_exporter.hpp"
#include "rftrace/exporters/geojson_exporter.hpp"
#include "rftrace/exporters/gltf_exporter.hpp"
#include "rftrace/exporters/json_exporter.hpp"

#ifndef RFTRACE_CLI_VERSION
#define RFTRACE_CLI_VERSION "unknown"
#endif

using namespace rftrace;
using namespace rftrace::cli;

namespace {

void printHelp() {
  std::cout <<
      "rftrace-result-converter — convert an rftrace-cli result to another format\n"
      "\n"
      "Usage: rftrace-result-converter --in <result.json> --out <path>\n"
      "\n"
      "  --in <result.json>   input result (point results only)\n"
      "  --out <path>         output; format from extension (.csv/.geojson/.gltf/.json)\n"
      "  -h, --help           show this help\n"
      "      --version        show version\n";
}

std::string readFile(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open input '" + path + "'");
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void convertPoint(const std::string& inPath, const std::string& outPath,
                  OutFormat fmt) {
  const RFResult result = io::loadResultJson(inPath);
  switch (fmt) {
    case OutFormat::Csv: io::exportReceiversCsv(result, outPath); break;
    case OutFormat::GeoJson: io::exportReceiversGeoJson(result, outPath); break;
    case OutFormat::Gltf: io::exportPathsGltf(result, outPath); break;
    case OutFormat::Json: io::exportResultJson(result, outPath); break;
    default:
      throw std::runtime_error("unsupported conversion: point result -> '" +
                               outPath + "' (use .csv/.geojson/.gltf/.json)");
  }
}

int convert(const ArgParser& args) {
  const std::string inPath = args.get("in");
  const std::string outPath = args.get("out");
  if (formatFromPath(inPath) != OutFormat::Json)
    throw std::runtime_error("input must be a .json result (got '" + inPath + "')");

  const std::string text = readFile(inPath);
  const OutFormat outFmt = formatFromPath(outPath);
  if (outFmt == OutFormat::Unknown)
    throw std::runtime_error("unknown output format for '" + outPath + "'");

  switch (resultKindFromJsonText(text)) {
    case ResultKind::Point:
      convertPoint(inPath, outPath, outFmt);
      break;
    case ResultKind::Coverage:
      throw std::runtime_error(
          "converting coverage results is not supported (no public loader)");
    case ResultKind::Route:
      throw std::runtime_error(
          "converting route results is not supported (no public loader)");
    case ResultKind::Unknown:
      throw std::runtime_error("could not determine result kind of '" + inPath +
                               "' (not an rftrace result?)");
  }
  std::cout << "wrote " << outPath << "\n";
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
      std::cout << "rftrace-result-converter " << RFTRACE_CLI_VERSION << "\n";
      return 0;
    }
    return convert(args);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
