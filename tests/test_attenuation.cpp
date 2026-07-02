// Atmospheric (rain ITU-R P.838, gaseous ITU-R P.676) and vegetation
// (Weissberger / ITU-R P.833) attenuation (Phase 7, R5). Covers the formula
// modules' reference behavior and their additive, default-off wiring into the
// per-path budget via the PropagationContext hook.

#include <gtest/gtest.h>

#include <cmath>

#include "rftrace/rf/atmospheric.hpp"
#include "rftrace/rf/free_space_path_loss.hpp"
#include "rftrace/rf/vegetation.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;
using rftrace::rf::foliageLossDb;
using rftrace::rf::gaseousSpecificAttenuationDbPerKm;
using rftrace::rf::rainSpecificAttenuationDbPerKm;

namespace {

Transmitter mkTx(const Vec3& p, double freqHz = 3.5e9) {
  Transmitter t;
  t.id = "tx";
  t.position = p;
  t.frequencyHz = freqHz;
  t.powerDbm = 43.0;
  return t;
}

Receiver mkRx(const Vec3& p) {
  Receiver r;
  r.id = "rx";
  r.position = p;
  return r;
}

// Two axis-aligned quads at x=x0 and x=x1 spanning a wide y/z box: a closed
// "slab" of the given material that a segment along +x crosses twice (enter +
// exit), giving an in-foliage depth of (x1 - x0).
void addSlab(Scene& s, const std::string& material, double x0, double x1) {
  auto quad = [&](double x) {
    const Vec3 a{x, -50, -50}, b{x, 50, -50}, c{x, 50, 50}, d{x, -50, 50};
    return std::vector<Triangle>{Triangle{a, b, c}, Triangle{a, c, d}};
  };
  s.addMesh(quad(x0), material);
  s.addMesh(quad(x1), material);
}

}  // namespace

// --- Rain (ITU-R P.838) ------------------------------------------------------

// gamma_R = k·R^alpha increases monotonically with rain rate at a fixed mmWave
// frequency, and is zero at zero rate.
TEST(Attenuation, RainScalesWithRate) {
  const double f = 30e9;
  EXPECT_EQ(rainSpecificAttenuationDbPerKm(f, 0.0), 0.0);
  const double g10 = rainSpecificAttenuationDbPerKm(f, 10.0);
  const double g25 = rainSpecificAttenuationDbPerKm(f, 25.0);
  const double g50 = rainSpecificAttenuationDbPerKm(f, 50.0);
  EXPECT_GT(g10, 0.0);
  EXPECT_GT(g25, g10);
  EXPECT_GT(g50, g25);
}

// Rain matters mainly at mmWave: below ~5 GHz the specific attenuation is
// negligible compared with a mmWave frequency at the same (heavy) rain rate.
TEST(Attenuation, RainNegligibleBelowFiveGHz) {
  const double R = 25.0;  // heavy rain
  const double gLow = rainSpecificAttenuationDbPerKm(2.4e9, R);
  const double gHigh = rainSpecificAttenuationDbPerKm(30e9, R);
  EXPECT_LT(gLow, 0.1);          // ~0 dB/km at 2.4 GHz
  EXPECT_GT(gHigh, 3.0);         // multiple dB/km at 30 GHz
  EXPECT_GT(gHigh, 30.0 * gLow); // orders of magnitude larger
}

// --- Gaseous (ITU-R P.676) ---------------------------------------------------

// Gaseous specific attenuation is positive and, applied over a path, scales
// linearly with the path length.
TEST(Attenuation, GaseousAppliedOverLength) {
  const double g = gaseousSpecificAttenuationDbPerKm(30e9);
  EXPECT_GT(g, 0.0);
  const double lossShort = g * 1.0;   // 1 km
  const double lossLong = g * 5.0;    // 5 km
  EXPECT_NEAR(lossLong, 5.0 * lossShort, 1e-12);
}

// --- Vegetation (Weissberger / P.833) ---------------------------------------

// Foliage loss is 0 at zero depth and grows sub-linearly with depth in the
// Weissberger d^0.588 regime (doubling depth less than doubles the loss).
TEST(Attenuation, FoliageGrowsSubLinearlyFromZero) {
  const double f = 3.5e9;
  EXPECT_EQ(foliageLossDb(f, 0.0), 0.0);

  const double l50 = foliageLossDb(f, 50.0);
  const double l100 = foliageLossDb(f, 100.0);
  const double l200 = foliageLossDb(f, 200.0);
  EXPECT_GT(l50, 0.0);
  EXPECT_GT(l100, l50);
  EXPECT_GT(l200, l100);
  // Sub-linear: L(2d) < 2·L(d).
  EXPECT_LT(l100, 2.0 * l50);
  EXPECT_LT(l200, 2.0 * l100);
}

// The loss is clamped to a physically reasonable maximum for very deep foliage.
TEST(Attenuation, FoliageBounded) {
  const double huge = foliageLossDb(30e9, 100000.0);
  EXPECT_LE(huge, 40.0 + 1e-9);
}

// --- End-to-end wiring into the per-path budget ------------------------------

// A LOS path crossing a vegetation-material slab loses exactly the Weissberger
// foliage dB for the traversed depth, on top of free-space path loss.
TEST(Attenuation, PathThroughVegetationLosesFoliageDb) {
  Scene s;
  s.addMaterial(materials::preset("vegetation"));
  addSlab(s, "vegetation", -10.0, 10.0);  // 20 m thick foliage slab
  s.addTransmitter(mkTx({-100, 0, 0}));
  s.addReceiver(mkRx({100, 0, 0}));

  SimulationSettings st;
  st.maxReflections = 0;         // isolate the LOS path
  st.enableVegetation = true;

  const RFResult r = Simulator(st).run(s);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_EQ(rr->paths.size(), 1u);
  const RFPath& p = rr->paths[0];
  EXPECT_EQ(p.type, PathType::LOS);

  const double dist = 200.0;
  const double fspl = rf::freeSpacePathLossDb(dist, 3.5e9);
  const double foliage = foliageLossDb(3.5e9, 20.0);
  EXPECT_GT(foliage, 0.0);
  EXPECT_NEAR(p.pathLossDb, fspl + foliage, 1e-6);
  EXPECT_NEAR(p.receivedPowerDbm, 43.0 - (fspl + foliage), 1e-6);
}

// Rain + gaseous attenuation are added to a clear LOS path as (γ_R + γ_g)·L.
TEST(Attenuation, PathGainsAtmosphericLoss) {
  Scene s;  // empty scene: unobstructed LOS
  const double freq = 30e9;
  s.addTransmitter(mkTx({0, 0, 0}, freq));
  s.addReceiver(mkRx({1000, 0, 0}));  // 1 km => 1.0 km path

  SimulationSettings st;
  st.maxReflections = 0;
  st.enableRain = true;
  st.rainRateMmPerHr = 25.0;
  st.enableGaseousAttenuation = true;

  const RFResult r = Simulator(st).run(s);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_EQ(rr->paths.size(), 1u);
  const RFPath& p = rr->paths[0];

  const double lenKm = 1.0;
  const double fspl = rf::freeSpacePathLossDb(1000.0, freq);
  const double atmos =
      (rainSpecificAttenuationDbPerKm(freq, 25.0) +
       gaseousSpecificAttenuationDbPerKm(freq)) *
      lenKm;
  EXPECT_GT(atmos, 0.0);
  EXPECT_NEAR(p.pathLossDb, fspl + atmos, 1e-6);
}

// With every attenuation flag off (the default), the budget is exactly the
// Phase 1/2 free-space result — no extra loss, bit-for-bit.
TEST(Attenuation, DisabledEqualsPhase12Budget) {
  Scene s;
  const double freq = 30e9;
  s.addTransmitter(mkTx({0, 0, 0}, freq));
  s.addReceiver(mkRx({1000, 0, 0}));

  SimulationSettings st;  // all attenuation flags default-off
  st.maxReflections = 0;

  const RFResult r = Simulator(st).run(s);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_EQ(rr->paths.size(), 1u);

  const double fspl = rf::freeSpacePathLossDb(1000.0, freq);
  EXPECT_NEAR(rr->paths[0].pathLossDb, fspl, 1e-12);
  EXPECT_NEAR(rr->paths[0].receivedPowerDbm, 43.0 - fspl, 1e-12);
}

// A vegetation slab that would block LOS in Phase 1/2 still blocks it when
// vegetation attenuation is OFF (default behavior preserved).
TEST(Attenuation, VegetationDisabledStillBlocks) {
  Scene s;
  s.addMaterial(materials::preset("vegetation"));
  addSlab(s, "vegetation", -10.0, 10.0);
  s.addTransmitter(mkTx({-100, 0, 0}));
  s.addReceiver(mkRx({100, 0, 0}));

  SimulationSettings st;
  st.maxReflections = 0;
  st.enableVegetation = false;  // default

  const RFResult r = Simulator(st).run(s);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  EXPECT_TRUE(rr->paths.empty());  // foliage opaque to LOS when disabled
}
