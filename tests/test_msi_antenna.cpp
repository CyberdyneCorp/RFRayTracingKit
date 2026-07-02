#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "rftrace/antenna.hpp"
#include "rftrace/importers/msi_importer.hpp"
#include "rftrace/scene.hpp"

using namespace rftrace;
namespace fs = std::filesystem;

namespace {
std::string writeTemp(const std::string& name, const std::string& content) {
  const fs::path p = fs::path(testing::TempDir()) / name;
  std::ofstream out(p);
  out << content;
  out.close();
  return p.string();
}

// A small Planet/MSI fixture. GAIN is in dBd, so the imported peak is
// 15.0 + 2.15 = 17.15 dBi. HORIZONTAL/VERTICAL rows are "angle attenuation"
// (dB down from peak); the importer stores them as negative relative dB.
const char* kMsi = R"(NAME Test Sector Antenna
FREQUENCY 900
GAIN 15.0 dBd
TILT 0
COMMENT synthetic pattern for the unit test
HORIZONTAL 5
0 0.0
45 5.0
90 10.0
135 15.0
180 20.0
VERTICAL 5
0 0.0
45 3.0
90 6.0
135 9.0
180 12.0
)";
}  // namespace

TEST(MsiImport, ParsesPeakGainAndBothCuts) {
  const std::string path = writeTemp("sector.msi", kMsi);
  const AntennaPattern pat = io::loadMsiAntenna(path);

  EXPECT_FALSE(pat.omni);
  EXPECT_NEAR(pat.peakGainDbi, 17.15, 1e-9);  // 15.0 dBd + 2.15
  EXPECT_EQ(pat.azimuthCutDb.size(), 5u);
  EXPECT_EQ(pat.verticalCutDb.size(), 5u);
  // Attenuations are stored as negative relative dB.
  EXPECT_NEAR(pat.azimuthCutDb[2].second, -10.0, 1e-9);  // 90° H
  EXPECT_NEAR(pat.verticalCutDb[2].second, -6.0, 1e-9);  // 90° V
}

TEST(MsiImport, BoresightReturnsPeakGain) {
  const AntennaPattern pat = io::loadMsiAntenna(writeTemp("b.msi", kMsi));
  EXPECT_NEAR(pat.gainTowards({1, 0, 0}), 17.15, 1e-9);
}

TEST(MsiImport, HorizontalRollOffMatchesTable) {
  const AntennaPattern pat = io::loadMsiAntenna(writeTemp("h.msi", kMsi));
  // (0,1,0): azimuth 90°, elevation 0° -> peak + H(90) + V(0) = 17.15 - 10.
  EXPECT_NEAR(pat.gainTowards({0, 1, 0}), 7.15, 1e-6);
}

TEST(MsiImport, VerticalRollOffMatchesTable) {
  const AntennaPattern pat = io::loadMsiAntenna(writeTemp("v.msi", kMsi));
  // (0,0,1): azimuth 0°, elevation 90° -> peak + H(0) + V(90) = 17.15 - 6.
  EXPECT_NEAR(pat.gainTowards({0, 0, 1}), 11.15, 1e-6);
}

TEST(MsiImport, CombinedHorizontalAndVerticalRollOff) {
  const AntennaPattern pat = io::loadMsiAntenna(writeTemp("hv.msi", kMsi));
  // (0,1,1): azimuth 90° (horizontal proj is +Y), elevation 45°
  // -> peak + H(90) + V(45) = 17.15 - 10 - 3 = 4.15.
  EXPECT_NEAR(pat.gainTowards({0, 1, 1}), 4.15, 1e-6);
}

TEST(MsiImport, GainInDbiIsNotConverted) {
  const char* dbi =
      "NAME X\nGAIN 12.0 dBi\nHORIZONTAL 1\n0 0.0\nVERTICAL 1\n0 0.0\n";
  const AntennaPattern pat = io::loadMsiAntenna(writeTemp("dbi.msi", dbi));
  EXPECT_NEAR(pat.peakGainDbi, 12.0, 1e-9);
}

TEST(MsiImport, MissingFileThrows) {
  EXPECT_THROW(io::loadMsiAntenna("/no/such/file.msi"), SceneError);
}

TEST(MsiImport, FileWithoutTablesThrows) {
  const std::string path =
      writeTemp("notables.msi", "NAME X\nGAIN 10.0 dBi\n");
  EXPECT_THROW(io::loadMsiAntenna(path), SceneError);
}

TEST(MsiImport, TruncatedTableThrows) {
  // HORIZONTAL declares 5 rows but only 2 are present.
  const std::string path = writeTemp(
      "trunc.msi", "GAIN 10 dBi\nHORIZONTAL 5\n0 0.0\n45 5.0\n");
  EXPECT_THROW(io::loadMsiAntenna(path), SceneError);
}

// Backward compatibility: an azimuth-only pattern with an empty vertical cut
// behaves exactly as before (the vertical cut contributes 0 dB), and an omni
// pattern is unaffected in every direction.
TEST(MsiImport, EmptyVerticalCutIsBackwardCompatible) {
  AntennaPattern pat;
  pat.omni = false;
  pat.peakGainDbi = 10.0;
  pat.boresight = {1, 0, 0};
  pat.up = {0, 0, 1};
  pat.azimuthCutDb = {{0.0, 0.0}, {90.0, -10.0}, {180.0, -20.0}};
  ASSERT_TRUE(pat.verticalCutDb.empty());

  EXPECT_NEAR(pat.gainTowards({1, 0, 0}), 10.0, 1e-9);
  EXPECT_NEAR(pat.gainTowards({0, 1, 0}), 0.0, 1e-6);
  EXPECT_NEAR(pat.gainTowards({1, 1, 0}), 5.0, 1e-6);
  // A purely vertical direction still only pays the azimuth cut (V empty).
  EXPECT_NEAR(pat.gainTowards({0, 0, 1}), 10.0, 1e-6);

  AntennaPattern omni = AntennaPattern::Omnidirectional(3.0);
  EXPECT_NEAR(omni.gainTowards({0, 0, 1}), 3.0, 1e-12);
  EXPECT_NEAR(omni.gainTowards({1, 2, -3}), 3.0, 1e-12);
}
