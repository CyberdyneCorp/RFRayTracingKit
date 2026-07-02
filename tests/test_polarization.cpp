// Polarization (D3): Jones-vector mismatch loss and depolarizing reflection.
// References are ANALYTIC, not trends:
//   * co-polar V-V  -> 0 dB   (normalized inner product = 1)
//   * 45 deg linear -> 3.0103 dB  (ratio = |cos45|^2 = 1/2)
//   * orthogonal V-H, RHCP-LHCP -> clamped sentinel (would be +inf)
//   * RHCP-RHCP -> 0 dB
//   * reflection with |Gamma_TE| != |Gamma_TM| rotates/ellipticizes the Jones
//     vector by exactly the Fresnel coefficients
//   * the default (V-V) per-path budget is unchanged vs the FSPL-only value

#include <gtest/gtest.h>

#include <cmath>
#include <complex>

#include "rftrace/rf/free_space_path_loss.hpp"
#include "rftrace/rf/fresnel.hpp"
#include "rftrace/rf/polarization.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;
using rftrace::rf::Complex;
using rftrace::rf::Jones;
using rftrace::rf::jonesFor;
using rftrace::rf::kMaxPolarizationMismatchDb;
using rftrace::rf::polarizationMismatchDb;
using rftrace::rf::reflectDepolarize;

namespace {

// ---------------------------------------------------------------------------
// Mismatch loss — analytic references
// ---------------------------------------------------------------------------

TEST(Polarization, CoPolarVerticalIsLossless) {
  EXPECT_DOUBLE_EQ(polarizationMismatchDb(Polarization::Vertical,
                                          Polarization::Vertical),
                   0.0);
  EXPECT_DOUBLE_EQ(
      polarizationMismatchDb(jonesFor(Polarization::Vertical),
                             jonesFor(Polarization::Vertical)),
      0.0);
}

TEST(Polarization, CoPolarHorizontalIsLossless) {
  EXPECT_DOUBLE_EQ(polarizationMismatchDb(Polarization::Horizontal,
                                          Polarization::Horizontal),
                   0.0);
}

TEST(Polarization, FortyFiveDegreeLinearIs3dB) {
  // 45 deg linear state in the (V, H) basis: (1, 1)/sqrt(2).
  const double s = 0.7071067811865475244;
  const Jones tx = jonesFor(Polarization::Vertical);
  const Jones rx45{Complex(s, 0.0), Complex(s, 0.0)};
  // ratio = |<rx,tx>|^2 = |s|^2 = 1/2  ->  -10*log10(0.5) = 3.0103 dB.
  EXPECT_NEAR(polarizationMismatchDb(tx, rx45), 3.0102999566, 1e-6);
}

TEST(Polarization, OrthogonalVerticalHorizontalHitsSentinel) {
  const double loss = polarizationMismatchDb(Polarization::Vertical,
                                             Polarization::Horizontal);
  EXPECT_GE(loss, 50.0);
  EXPECT_DOUBLE_EQ(loss, kMaxPolarizationMismatchDb);  // clamped, not +inf
  EXPECT_TRUE(std::isfinite(loss));
}

TEST(Polarization, CircularOrthogonalRhcpLhcpHitsSentinel) {
  // <LHCP, RHCP> = 0 exactly: (1,+j)/sqrt2 vs (1,-j)/sqrt2.
  const double loss =
      polarizationMismatchDb(Polarization::RHCP, Polarization::LHCP);
  EXPECT_GE(loss, 50.0);
  EXPECT_DOUBLE_EQ(loss, kMaxPolarizationMismatchDb);
}

TEST(Polarization, CoPolarCircularIsLossless) {
  EXPECT_NEAR(
      polarizationMismatchDb(Polarization::RHCP, Polarization::RHCP), 0.0,
      1e-12);
  EXPECT_NEAR(
      polarizationMismatchDb(Polarization::LHCP, Polarization::LHCP), 0.0,
      1e-12);
}

TEST(Polarization, CircularToLinearIs3dB) {
  // Any circular state onto any linear antenna loses exactly half the power.
  EXPECT_NEAR(
      polarizationMismatchDb(Polarization::RHCP, Polarization::Vertical),
      3.0102999566, 1e-6);
  EXPECT_NEAR(
      polarizationMismatchDb(Polarization::LHCP, Polarization::Horizontal),
      3.0102999566, 1e-6);
}

TEST(Polarization, NoneDisablesMismatch) {
  EXPECT_DOUBLE_EQ(
      polarizationMismatchDb(Polarization::None, Polarization::Horizontal),
      0.0);
  EXPECT_DOUBLE_EQ(
      polarizationMismatchDb(Polarization::Vertical, Polarization::None), 0.0);
}

TEST(Polarization, MismatchIsSymmetric) {
  const Jones a = jonesFor(Polarization::RHCP);
  const Jones b{Complex(0.6, 0.0), Complex(0.8, 0.0)};  // arbitrary linear
  EXPECT_NEAR(polarizationMismatchDb(a, b), polarizationMismatchDb(b, a),
              1e-12);
}

// ---------------------------------------------------------------------------
// Depolarizing reflection — Fresnel-exact
// ---------------------------------------------------------------------------

TEST(Polarization, ReflectionScalesComponentsByFresnelCoefficients) {
  using rftrace::rf::complexPermittivity;
  using rftrace::rf::fresnelReflectionCoefficient;
  using rftrace::rf::FresnelPolarization;

  // Concrete-like medium at 3.5 GHz, oblique incidence 60 deg from normal.
  const double freq = 3.5e9;
  const Complex epsc = complexPermittivity(5.31, 0.0326, freq);
  const double theta = 60.0 * rftrace::constants::pi / 180.0;
  const Complex te =
      fresnelReflectionCoefficient(epsc, theta, FresnelPolarization::TE);
  const Complex tm =
      fresnelReflectionCoefficient(epsc, theta, FresnelPolarization::TM);
  // TE and TM must differ so reflection actually depolarizes.
  ASSERT_GT(std::abs(std::abs(te) - std::abs(tm)), 1e-3);

  // Incident 45 deg linear: equal V and H components.
  const double s = 0.7071067811865475244;
  const Jones incident{Complex(s, 0.0), Complex(s, 0.0)};
  const Jones out = reflectDepolarize(incident, te, tm);

  // Vertical component scaled by TM, Horizontal by TE — bit-for-bit.
  EXPECT_EQ(out.v, tm * incident.v);
  EXPECT_EQ(out.h, te * incident.h);

  // The equal-magnitude incident state is no longer balanced: |out.v|/|out.h|
  // equals |tm|/|te| != 1, i.e. the polarization is rotated/ellipticized.
  EXPECT_GT(std::abs(std::abs(out.v) - std::abs(out.h)), 1e-3);
}

TEST(Polarization, IdentityReflectionPreservesPolarization) {
  // Unit coefficients (perfect conductor idealization) leave the state intact.
  const Jones j = jonesFor(Polarization::RHCP);
  const Jones out = reflectDepolarize(j, Complex(1.0, 0.0), Complex(1.0, 0.0));
  EXPECT_EQ(out.v, j.v);
  EXPECT_EQ(out.h, j.h);
}

// ---------------------------------------------------------------------------
// Budget integration — default co-polar leaves the budget unchanged
// ---------------------------------------------------------------------------

Transmitter makeTx(const Vec3& p, Polarization pol) {
  Transmitter tx;
  tx.id = "tx";
  tx.position = p;
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  tx.polarization = pol;
  return tx;
}
Receiver makeRx(const Vec3& p, Polarization pol) {
  Receiver rx;
  rx.id = "rx";
  rx.position = p;
  rx.polarization = pol;
  return rx;
}

TEST(Polarization, DefaultCoPolarBudgetMatchesFspl) {
  Scene scene;
  const Vec3 txp{0, 0, 10};
  const Vec3 rxp{100, 0, 10};
  scene.addTransmitter(makeTx(txp, Polarization::Vertical));
  scene.addReceiver(makeRx(rxp, Polarization::Vertical));

  SimulationSettings s;
  s.maxReflections = 0;
  const RFResult r = Simulator(s).run(scene);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_EQ(rr->paths.size(), 1u);

  // Omni 0 dBi both ends: received power is exactly txPower - FSPL, i.e. the
  // polarization term contributes 0 dB.
  const double dist = (rxp - txp).norm();
  const double fspl = rf::freeSpacePathLossDb(dist, 3.5e9);
  EXPECT_DOUBLE_EQ(rr->paths[0].pathLossDb, fspl);
  EXPECT_DOUBLE_EQ(rr->paths[0].receivedPowerDbm, 43.0 - fspl);
  // Tracked path polarization defaults to the transmitter (co-polar Vertical).
  EXPECT_EQ(rr->paths[0].polarization.v, Complex(1.0, 0.0));
  EXPECT_EQ(rr->paths[0].polarization.h, Complex(0.0, 0.0));
}

TEST(Polarization, CrossPolarReceiverLosesPower) {
  Scene scene;
  const Vec3 txp{0, 0, 10};
  const Vec3 rxp{100, 0, 10};
  scene.addTransmitter(makeTx(txp, Polarization::Vertical));
  scene.addReceiver(makeRx(rxp, Polarization::Horizontal));

  SimulationSettings s;
  s.maxReflections = 0;
  const RFResult r = Simulator(s).run(scene);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_EQ(rr->paths.size(), 1u);

  const double dist = (rxp - txp).norm();
  const double fspl = rf::freeSpacePathLossDb(dist, 3.5e9);
  // Orthogonal V-H adds the sentinel mismatch to the path loss.
  EXPECT_DOUBLE_EQ(rr->paths[0].pathLossDb,
                   fspl + rf::kMaxPolarizationMismatchDb);
  EXPECT_DOUBLE_EQ(rr->paths[0].receivedPowerDbm,
                   43.0 - fspl - rf::kMaxPolarizationMismatchDb);
}

}  // namespace
