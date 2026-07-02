#include <gtest/gtest.h>

#include <cmath>

#include "rftrace/rf/diffraction.hpp"
#include "rftrace/rf/utd.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;

// --- UTD as a selectable diffraction path model ------------------------------

// The UTD half-plane loss tracks the ITU-R knife-edge loss (a knife edge is a
// conducting half-plane): ~6 dB at grazing and within a fraction of a dB across v.
TEST(RfWiring, UtdLossTracksKnifeEdge) {
  EXPECT_NEAR(rf::utdDiffractionLossDb(0.0), rf::knifeEdgeLossDb(0.0), 0.3);
  double prev = -1e9;
  for (double v : {0.0, 0.5, 1.0, 2.0, 3.0}) {
    const double utd = rf::utdDiffractionLossDb(v);
    EXPECT_NEAR(utd, rf::knifeEdgeLossDb(v), 0.8) << "v=" << v;
    EXPECT_GT(utd, prev);  // monotonically increasing into shadow
    prev = utd;
  }
  EXPECT_EQ(rf::utdDiffractionLossDb(-1.0), 0.0);  // ample clearance
}

namespace {
// tx and rx on opposite sides of a wall (LOS blocked) so a diffracted path forms.
Scene shadowedScene() {
  Scene scene;
  scene.addMesh({Triangle{{0, -40, 0}, {0, 40, 0}, {0, 40, 8}},
                 Triangle{{0, -40, 0}, {0, 40, 8}, {0, -40, 8}}}, "");
  Transmitter tx;
  tx.id = "tx"; tx.position = {-60, 0, 5}; tx.frequencyHz = 3.5e9; tx.powerDbm = 43;
  scene.addTransmitter(tx);
  Receiver rx; rx.id = "rx"; rx.position = {60, 0, 5};
  scene.addReceiver(rx);
  return scene;
}
double diffractionLoss(DiffractionModel model) {
  Scene s = shadowedScene();
  SimulationSettings st;
  st.maxReflections = 0;
  st.enableDiffraction = true;
  st.diffractionModel = model;
  const RFResult res = Simulator(st).run(s);
  const auto* r = res.receiver("rx");
  for (const auto& p : r->paths)
    if (p.type == PathType::Diffraction) return p.pathLossDb;
  return -1.0;
}
}  // namespace

TEST(RfWiring, UtdModelSelectableAndClose) {
  const double single = diffractionLoss(DiffractionModel::SingleEdge);
  const double utd = diffractionLoss(DiffractionModel::UTD);
  ASSERT_GT(single, 0.0) << "SingleEdge produced no diffraction path";
  ASSERT_GT(utd, 0.0) << "UTD produced no diffraction path";
  EXPECT_TRUE(std::isfinite(utd));
  EXPECT_NEAR(utd, single, 1.0);  // UTD half-plane ~ knife-edge
}

// --- Reflection depolarization (opt-in) --------------------------------------

namespace {
double reflectionPower(bool depol) {
  Scene s;
  s.addMaterial(materials::preset("concrete"));
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  s.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  // Circular polarization has both transverse components, so unequal TE/TM at
  // the bounce makes the reflected wave elliptical (a pure V wave would stay V).
  Transmitter tx;
  tx.id = "tx"; tx.position = {100, 20, 20}; tx.frequencyHz = 3.5e9; tx.powerDbm = 43;
  tx.polarization = Polarization::RHCP;
  s.addTransmitter(tx);
  Receiver rx; rx.id = "rx"; rx.position = {200, 20, 10};
  rx.polarization = Polarization::RHCP;
  s.addReceiver(rx);
  SimulationSettings st;
  st.maxReflections = 1;
  st.enableDepolarization = depol;
  const RFResult res = Simulator(st).run(s);
  const auto* r = res.receiver("rx");
  for (const auto& p : r->paths)
    if (p.type == PathType::Reflection) return p.receivedPowerDbm;
  return 1e9;
}
}  // namespace

TEST(RfWiring, ReflectionDepolarizationIsOptInAndLossy) {
  const double off = reflectionPower(false);
  const double on = reflectionPower(true);
  ASSERT_LT(off, 1e8);
  ASSERT_LT(on, 1e8);
  // Depolarization off: co-polar V/V reflection has 0 dB mismatch (default-neutral).
  // On: a V wave off concrete (TE != TM) becomes elliptical -> nonzero mismatch,
  // so the received power drops.
  EXPECT_LT(on, off - 0.1);
  EXPECT_GT(off - on, 0.0);
  EXPECT_LT(off - on, 20.0);  // a bounce depolarizes modestly, not catastrophically
}
