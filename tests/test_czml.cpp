#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>

#include "rftrace/exporters/czml_exporter.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;
using nlohmann::json;

namespace {

// A reflection scene that yields at least one receiver with ray paths.
Scene reflectionScene() {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  Transmitter tx;
  tx.id = "tx";
  tx.position = {100, 20, 20};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);
  Receiver rx;
  rx.id = "rx";
  rx.position = {200, 20, 10};
  scene.addReceiver(rx);
  return scene;
}

RFResult runOn(const Scene& scene) {
  SimulationSettings s;
  s.maxReflections = 1;
  return Simulator(s).run(scene);
}

std::size_t countPaths(const RFResult& r) {
  std::size_t n = 0;
  for (const auto& rx : r.receivers) n += rx.paths.size();
  return n;
}

}  // namespace

TEST(Czml, IsValidJsonWithDocumentPacket) {
  const RFResult r = runOn(reflectionScene());
  const std::string czml = io::resultToCzmlString(r);
  const json doc = json::parse(czml);  // throws if invalid

  ASSERT_TRUE(doc.is_array());
  ASSERT_FALSE(doc.empty());
  EXPECT_EQ(doc[0].at("id").get<std::string>(), "document");
  EXPECT_TRUE(doc[0].contains("version"));
}

TEST(Czml, PacketCountMatchesReceiversAndPaths) {
  const RFResult r = runOn(reflectionScene());
  ASSERT_FALSE(r.receivers.empty());
  const std::size_t expected = 1 + r.receivers.size() + countPaths(r);

  const json doc = json::parse(io::resultToCzmlString(r));
  EXPECT_EQ(doc.size(), expected);

  // One point packet per receiver, one polyline packet per path.
  std::size_t points = 0, polylines = 0;
  for (const auto& pkt : doc) {
    if (pkt.contains("point")) ++points;
    if (pkt.contains("polyline")) ++polylines;
  }
  EXPECT_EQ(points, r.receivers.size());
  EXPECT_EQ(polylines, countPaths(r));
}

TEST(Czml, NonGeoreferencedEmitsCartesian) {
  Scene scene = reflectionScene();  // no geo origin
  const RFResult r = runOn(scene);
  const json doc = json::parse(io::resultToCzmlString(r, scene));

  bool sawCartesian = false;
  for (const auto& pkt : doc) {
    if (pkt.contains("position")) {
      EXPECT_TRUE(pkt.at("position").contains("cartesian"));
      EXPECT_FALSE(pkt.at("position").contains("cartographicDegrees"));
      sawCartesian = true;
    }
  }
  EXPECT_TRUE(sawCartesian);
}

TEST(Czml, GeoreferencedEmitsCartographicDegrees) {
  Scene scene = reflectionScene();
  scene.setGeoOrigin(48.0, 11.0);
  const RFResult r = runOn(scene);
  const json doc = json::parse(io::resultToCzmlString(r, scene));

  bool sawReceiverPosition = false;
  for (const auto& pkt : doc) {
    const std::string id = pkt.value("id", "");
    if (id.rfind("receiver/", 0) == 0) {
      const json& pos = pkt.at("position");
      ASSERT_TRUE(pos.contains("cartographicDegrees"));
      const auto& coords = pos.at("cartographicDegrees");
      ASSERT_EQ(coords.size(), 3u);
      // Receiver rx is at local (200, 20, 10); inverse D1 about (48,11).
      const double lat0 = 48.0, lon0 = 11.0;
      const double mPerDegLon = 111320.0 * std::cos(lat0 * M_PI / 180.0);
      EXPECT_NEAR(coords[0].get<double>(), lon0 + 200.0 / mPerDegLon, 1e-6);
      EXPECT_NEAR(coords[1].get<double>(), lat0 + 20.0 / 110540.0, 1e-6);
      EXPECT_NEAR(coords[2].get<double>(), 10.0, 1e-9);
      sawReceiverPosition = true;
    }
    if (pkt.contains("polyline"))
      EXPECT_TRUE(pkt.at("polyline").at("positions").contains(
          "cartographicDegrees"));
  }
  EXPECT_TRUE(sawReceiverPosition);
}
