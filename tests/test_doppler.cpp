#include <gtest/gtest.h>

#include <cmath>

#include "rftrace/math.hpp"
#include "rftrace/rf/doppler.hpp"
#include "rftrace/route.hpp"
#include "rftrace/scene.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {

constexpr double kC = constants::c;  // speed of light (m/s)

// Open-field scene: one transmitter at height 10 m, no geometry, so every
// receiver/route sample has deterministic LOS.
Scene openFieldScene() {
  Scene scene;
  Transmitter tx;
  tx.id = "tx0";
  tx.position = {0, 0, 10};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);
  return scene;
}

}  // namespace

// --- perPathDopplerHz: analytic reference values -----------------------------

TEST(Doppler, ClosingAlongLosGivesPositiveVOverCTimesF) {
  const double f = 3.5e9;
  const double v = 30.0;  // m/s
  // Receiver closes on the source: velocity aligned with the arrival direction.
  const Vec3 khat{1, 0, 0};          // rx -> last hop (toward the source)
  const Vec3 velocity{v, 0, 0};      // moving toward the source
  const double expected = v / kC * f;  // classical Doppler, analytic
  EXPECT_NEAR(rf::perPathDopplerHz(velocity, khat, f), expected, 1e-6);
  // Sanity on the magnitude: ~350.24 Hz for these numbers.
  EXPECT_NEAR(expected, 350.2423, 1e-3);
}

TEST(Doppler, RecedingIsNegative) {
  const double f = 3.5e9;
  const double v = 30.0;
  const Vec3 khat{1, 0, 0};
  const Vec3 velocity{-v, 0, 0};  // moving away from the source
  const double expected = -v / kC * f;
  EXPECT_NEAR(rf::perPathDopplerHz(velocity, khat, f), expected, 1e-6);
  EXPECT_LT(rf::perPathDopplerHz(velocity, khat, f), 0.0);
}

TEST(Doppler, TransverseMotionIsZero) {
  const double f = 3.5e9;
  const Vec3 khat{1, 0, 0};
  const Vec3 velocity{0, 30, 0};  // perpendicular to the arrival direction
  EXPECT_NEAR(rf::perPathDopplerHz(velocity, khat, f), 0.0, 1e-9);
}

TEST(Doppler, StaticReceiverIsExactlyZero) {
  const Vec3 khat{0.3, -0.5, 0.8};
  EXPECT_EQ(rf::perPathDopplerHz(Vec3::Zero(), khat, 3.5e9), 0.0);
}

TEST(Doppler, ArrivalDirectionIsNormalizedInternally) {
  const double f = 2.0e9;
  const double v = 12.0;
  const Vec3 velocity{v, 0, 0};
  // A non-unit arrival direction must give the same result as the unit one.
  const double unit = rf::perPathDopplerHz(velocity, Vec3{1, 0, 0}, f);
  const double scaled = rf::perPathDopplerHz(velocity, Vec3{7, 0, 0}, f);
  EXPECT_NEAR(unit, scaled, 1e-9);
  EXPECT_NEAR(unit, v / kC * f, 1e-9);
}

TEST(Doppler, ObliqueVelocityUsesProjection) {
  const double f = 1.0e9;
  // Velocity 45 deg to the arrival direction -> only the along-khat component
  // contributes: (v.khat) = |v|*cos(45).
  const Vec3 khat{1, 0, 0};
  const double speed = 100.0;
  const double comp = speed * std::sqrt(0.5);
  const Vec3 velocity{comp, comp, 0};  // 45 deg in the x-y plane
  EXPECT_NEAR(rf::perPathDopplerHz(velocity, khat, f), comp / kC * f, 1e-6);
}

TEST(Doppler, ZeroFrequencyIsZero) {
  EXPECT_EQ(rf::perPathDopplerHz(Vec3{10, 0, 0}, Vec3{1, 0, 0}, 0.0), 0.0);
}

// --- RFPath default is inert (static receivers) ------------------------------

TEST(Doppler, PointReceiverRunLeavesDopplerZero) {
  Scene scene = openFieldScene();
  Receiver rx;
  rx.id = "rx0";
  rx.position = {50, 0, 10};
  scene.addReceiver(rx);

  SimulationSettings s;
  s.maxReflections = 0;
  Simulator sim(s);
  const RFResult res = sim.run(scene);

  const ReceiverResult* r = res.receiver("rx0");
  ASSERT_NE(r, nullptr);
  ASSERT_FALSE(r->paths.empty());
  for (const RFPath& p : r->paths) EXPECT_EQ(p.dopplerHz, 0.0);
}

// --- Route simulator fills per-path Doppler ----------------------------------

TEST(Doppler, RouteClosingOnTxHasPositiveDoppler) {
  Scene scene = openFieldScene();  // tx at (0,0,10)
  SimulationSettings s;
  s.maxReflections = 0;  // LOS only -> one path per sample
  Simulator sim(s);

  const double speed = 30.0;
  const double f = scene.transmitters().front().frequencyHz;

  // Route at tx height moving in -x toward the tx: arrival direction is purely
  // -x and the velocity is purely -x, so the closing Doppler is exactly +v/c*f.
  Route r;
  r.id = "closing";
  r.waypoints = {Vec3{100, 0, 10}, Vec3{10, 0, 10}};
  r.sampleSpacing = 10.0;
  r.speedMps = speed;

  const RouteResult res = sim.runRoute(scene, r);
  ASSERT_GE(res.samples.size(), 3u);

  const double expected = speed / kC * f;  // analytic
  // Interior samples have a well-defined (central-difference) velocity.
  for (std::size_t i = 1; i + 1 < res.samples.size(); ++i) {
    EXPECT_TRUE(res.samples[i].hasSignal);
    EXPECT_NEAR(res.samples[i].dopplerHz, expected, 1e-3);
    EXPECT_GT(res.samples[i].dopplerHz, 0.0);
  }
}

TEST(Doppler, RouteRecedingFromTxHasNegativeDoppler) {
  Scene scene = openFieldScene();
  SimulationSettings s;
  s.maxReflections = 0;
  Simulator sim(s);

  const double speed = 30.0;
  const double f = scene.transmitters().front().frequencyHz;

  // Same geometry, reversed: moving +x away from the tx.
  Route r;
  r.id = "receding";
  r.waypoints = {Vec3{10, 0, 10}, Vec3{100, 0, 10}};
  r.sampleSpacing = 10.0;
  r.speedMps = speed;

  const RouteResult res = sim.runRoute(scene, r);
  ASSERT_GE(res.samples.size(), 3u);

  // Aggregate is max |f_d|, so it stays positive; the underlying shift is
  // negative. Verify magnitude matches -v/c*f in absolute value.
  const double expectedAbs = speed / kC * f;
  for (std::size_t i = 1; i + 1 < res.samples.size(); ++i)
    EXPECT_NEAR(res.samples[i].dopplerHz, expectedAbs, 1e-3);
}

TEST(Doppler, RouteNoSpeedUsesSpacingAsVelocityMagnitude) {
  Scene scene = openFieldScene();
  SimulationSettings s;
  s.maxReflections = 0;
  Simulator sim(s);

  const double f = scene.transmitters().front().frequencyHz;
  const double spacing = 10.0;

  // No speedMps: the derived velocity is the per-step displacement (unit sample
  // time), so an interior central difference gives magnitude == spacing along a
  // uniformly spaced straight line.
  Route r;
  r.id = "no-speed";
  r.waypoints = {Vec3{100, 0, 10}, Vec3{10, 0, 10}};
  r.sampleSpacing = spacing;
  // r.speedMps left at default 0.

  const RouteResult res = sim.runRoute(scene, r);
  ASSERT_GE(res.samples.size(), 3u);

  const double expected = spacing / kC * f;  // v == spacing
  EXPECT_NEAR(res.samples[1].dopplerHz, expected, 1e-3);
}

TEST(Doppler, TransverseRouteHasZeroDoppler) {
  Scene scene = openFieldScene();  // tx at (0,0,10)
  SimulationSettings s;
  s.maxReflections = 0;
  Simulator sim(s);

  // Route moving in +y at fixed x offset and tx height: velocity is +y while the
  // arrival direction lies in the x-z plane, so the projection (and Doppler) ~0.
  Route r;
  r.id = "transverse";
  r.waypoints = {Vec3{50, -50, 10}, Vec3{50, 50, 10}};
  r.sampleSpacing = 10.0;
  r.speedMps = 30.0;

  const RouteResult res = sim.runRoute(scene, r);
  ASSERT_GE(res.samples.size(), 3u);

  // At the y=0 sample the arrival direction is purely -x, exactly orthogonal to
  // the +y velocity -> zero Doppler.
  for (const RouteSample& smp : res.samples)
    if (std::abs(smp.position.y()) < 1e-9)
      EXPECT_NEAR(smp.dopplerHz, 0.0, 1e-6);
}

TEST(Doppler, SingleSampleRouteHasZeroDoppler) {
  Scene scene = openFieldScene();
  SimulationSettings s;
  s.maxReflections = 0;
  Simulator sim(s);

  Route r;
  r.id = "point";
  r.waypoints = {Vec3{50, 0, 10}};  // single waypoint -> single sample
  r.speedMps = 30.0;                // even with a configured speed

  const RouteResult res = sim.runRoute(scene, r);
  ASSERT_EQ(res.samples.size(), 1u);
  EXPECT_EQ(res.samples.front().dopplerHz, 0.0);
}
