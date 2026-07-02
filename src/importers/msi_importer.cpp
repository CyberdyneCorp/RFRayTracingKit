#include "rftrace/importers/msi_importer.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rftrace/scene.hpp"

namespace rftrace::io {

namespace {

/// dBd → dBi conversion: a dipole has 2.15 dBi of gain over an isotrope.
constexpr double kDbdToDbi = 2.15;

using Table = std::vector<std::pair<double, double>>;

std::string upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return s;
}

/// A non-blank, non-comment line split into whitespace-separated tokens, or an
/// empty vector for lines that carry no data.
std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> tokens;
  std::istringstream ss(line);
  std::string tok;
  while (ss >> tok) tokens.push_back(tok);
  if (!tokens.empty()) {
    const char first = tokens.front().empty() ? ' ' : tokens.front()[0];
    if (first == '#' || first == ';' || first == '!') tokens.clear();
  }
  return tokens;
}

/// Parse the `GAIN <value> [dBd|dBi]` value into dBi.
double parseGain(const std::vector<std::string>& tokens, const std::string& p) {
  if (tokens.size() < 2)
    throw SceneError("MSI file '" + p + "': GAIN line missing a value");
  double value = 0.0;
  try {
    value = std::stod(tokens[1]);
  } catch (const std::exception&) {
    throw SceneError("MSI file '" + p + "': GAIN value '" + tokens[1] +
                     "' is not a number");
  }
  if (tokens.size() >= 3 && upper(tokens[2]) == "DBD") value += kDbdToDbi;
  return value;
}

/// Read `count` `angle attenuation` rows following a HORIZONTAL/VERTICAL header
/// into a cut table, storing attenuations as negative relative-dB. Advances the
/// line index past the consumed rows.
Table readCut(const std::vector<std::string>& lines, std::size_t& i,
              int count, const std::string& kw, const std::string& p) {
  if (count < 0)
    throw SceneError("MSI file '" + p + "': negative " + kw + " count");
  Table table;
  table.reserve(static_cast<std::size_t>(count));
  int read = 0;
  while (read < count && i < lines.size()) {
    const auto tokens = tokenize(lines[i]);
    if (tokens.empty()) {
      ++i;
      continue;
    }
    if (tokens.size() < 2)
      throw SceneError("MSI file '" + p + "': malformed " + kw +
                       " row '" + lines[i] + "'");
    try {
      const double angle = std::stod(tokens[0]);
      const double atten = std::stod(tokens[1]);
      table.emplace_back(angle, -atten);
    } catch (const std::exception&) {
      throw SceneError("MSI file '" + p + "': non-numeric " + kw + " row '" +
                       lines[i] + "'");
    }
    ++read;
    ++i;
  }
  if (read < count)
    throw SceneError("MSI file '" + p + "': " + kw + " declared " +
                     std::to_string(count) + " rows but only " +
                     std::to_string(read) + " found");
  std::sort(table.begin(), table.end());
  return table;
}

int parseCount(const std::vector<std::string>& tokens, const std::string& kw,
               const std::string& p) {
  if (tokens.size() < 2)
    throw SceneError("MSI file '" + p + "': " + kw + " missing a count");
  try {
    return std::stoi(tokens[1]);
  } catch (const std::exception&) {
    throw SceneError("MSI file '" + p + "': " + kw + " count '" + tokens[1] +
                     "' is not an integer");
  }
}

}  // namespace

AntennaPattern loadMsiAntenna(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw SceneError("cannot open MSI antenna file '" + path + "'");

  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) lines.push_back(line);

  AntennaPattern pattern;
  pattern.omni = false;
  pattern.boresight = {1.0, 0.0, 0.0};
  pattern.up = {0.0, 0.0, 1.0};

  bool gainSeen = false;
  bool hSeen = false;
  bool vSeen = false;

  for (std::size_t i = 0; i < lines.size();) {
    const auto tokens = tokenize(lines[i]);
    if (tokens.empty()) {
      ++i;
      continue;
    }
    const std::string kw = upper(tokens[0]);
    ++i;  // consume the keyword line; table rows (if any) follow
    if (kw == "GAIN") {
      pattern.peakGainDbi = parseGain(tokens, path);
      gainSeen = true;
    } else if (kw == "HORIZONTAL") {
      pattern.azimuthCutDb =
          readCut(lines, i, parseCount(tokens, kw, path), kw, path);
      hSeen = true;
    } else if (kw == "VERTICAL") {
      pattern.verticalCutDb =
          readCut(lines, i, parseCount(tokens, kw, path), kw, path);
      vSeen = true;
    }
    // NAME/FREQUENCY/TILT/POLARIZATION/COMMENT and unknown keywords are ignored.
  }

  if (!gainSeen)
    throw SceneError("MSI file '" + path + "': no GAIN line found");
  if (!hSeen && !vSeen)
    throw SceneError("MSI file '" + path +
                     "': no HORIZONTAL or VERTICAL table found");
  return pattern;
}

}  // namespace rftrace::io
