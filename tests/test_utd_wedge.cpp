// Geometry-driven single-wedge UTD path model (Phase 1). These are the five
// MANDATED physical gates plus wedge-geometry extraction unit tests. UTD has no
// closed-form scene answer, so correctness is validated as physical properties:
//   1. reduce-to-knife-edge (n = 2 reproduces the ITU-R curve),
//   2. reciprocity (tx<->rx swap),
//   3. shadow-boundary continuity (no step/spike in the total field),
//   4. monotonic shadowing (loss grows deeper into shadow),
//   5. wedge-angle sensitivity + determinism.

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <optional>

#include "rftrace/math.hpp"
#include "rftrace/rf/diffraction.hpp"
#include "rftrace/rf/utd.hpp"
#include "rftrace/rf/utd_geometry.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;
using rftrace::rf::extractWedgeGeometry;
using rftrace::rf::knifeEdgeLossDb;
using rftrace::rf::utdDiffractionLossDb;
using rftrace::rf::utdWedgeCoefficient;
using rftrace::rf::utdWedgePathLossDb;
using rftrace::rf::WedgeBoundary;
using rftrace::rf::WedgeGeometry;
using Cd = std::complex<double>;

namespace {

constexpr double kPi = rftrace::constants::pi;
constexpr double kTwoPi = rftrace::constants::two_pi;

double deg(double d) { return d * kPi / 180.0; }

// Replicate the fixed reference geometry of utdDiffractionLossDb(v) but through
// the geometry-driven utdWedgePathLossDb: same L (via s = s' = d), phiPrime = pi/2,
// beta0 = pi/2, phi = 1.5*pi + delta. This must reproduce it to floating point.
double refWedgePathLossDb(double v) {
  const double d1 = 1000.0, d2 = 1000.0, lambda = 1.0;
  const double k = kTwoPi / lambda;
  const double scale = std::sqrt(2.0 * (d1 + d2) / (lambda * d1 * d2));
  const double h = v / scale;
  const double delta = h * (1.0 / d1 + 1.0 / d2);
  const double phi = 1.5 * kPi + delta;
  return utdWedgePathLossDb(phi, kPi / 2.0, kPi / 2.0, 2.0, k, /*s=*/d2,
                            /*sPrime=*/d1);
}

// ---------------------------------------------------------------------------
// GATE 1 — reduce-to-knife-edge (normalization pin)
// ---------------------------------------------------------------------------

// (a) Exact equivalence: for the reference geometry the geometry-driven loss is
// bit-close to the fixed-geometry half-plane reference, which tracks the ITU-R
// knife edge. This leaves NO free normalization scale.
TEST(UtdWedge, ReduceToKnifeEdgeExactEquivalence) {
  for (double v : {0.0, 0.5, 1.0, 2.0, 3.0}) {
    EXPECT_NEAR(refWedgePathLossDb(v), utdDiffractionLossDb(v), 1e-9)
        << "v=" << v;
    // and the reference itself is within the documented UTD/knife tolerance.
    EXPECT_NEAR(refWedgePathLossDb(v), knifeEdgeLossDb(v), 0.8) << "v=" << v;
  }
  // ~6 dB at the grazing shadow boundary (v = 0).
  EXPECT_NEAR(refWedgePathLossDb(0.0), 6.0, 0.5);
}

// ---------------------------------------------------------------------------
// GATE 2 — reciprocity
// ---------------------------------------------------------------------------

TEST(UtdWedge, ReciprocityUnitLevel) {
  const double k = kTwoPi / 0.1;
  const double phi = deg(250.0), phiPrime = deg(70.0), beta0 = deg(80.0);
  const double s = 40.0, sPrime = 90.0;
  const double fwd = utdWedgePathLossDb(phi, phiPrime, beta0, 2.0, k, s, sPrime);
  // Swap tx<->rx: phi<->phiPrime and s<->sPrime.
  const double rev = utdWedgePathLossDb(phiPrime, phi, beta0, 2.0, k, sPrime, s);
  EXPECT_NEAR(fwd, rev, 1e-9);

  // Same for a real wedge (n = 1.5).
  const double fwdW = utdWedgePathLossDb(deg(240.0), deg(60.0), beta0, 1.5, k,
                                         s, sPrime);
  const double revW = utdWedgePathLossDb(deg(60.0), deg(240.0), beta0, 1.5, k,
                                         sPrime, s);
  EXPECT_NEAR(fwdW, revW, 1e-9);
}

// ---------------------------------------------------------------------------
// GATE 3 — shadow-boundary continuity (field-level, geometry-driven angles)
// ---------------------------------------------------------------------------

// Half-plane (n = 2). Sweep the receiver across the incident shadow boundary,
// deriving phi/phiPrime from extractWedgeGeometry, and assert the total field
// GO + diffracted is continuous and equals half the incident field there.
TEST(UtdWedge, ShadowBoundaryContinuityFieldLevel) {
  const double lambda = 0.1;
  const double k = kTwoPi / lambda;
  const double rho = 5.0;

  // Edge along +y; half-plane material occupies the -z face (o-face vertex below).
  const Vec3 A{0, -5, 0}, B{0, 5, 0}, Q{0, 0, 0}, C0{0, 0, -1};
  // Transverse direction at angle a about the edge: (-sin a, 0, -cos a).
  auto dirAt = [](double a) { return Vec3{-std::sin(a), 0.0, -std::cos(a)}; };

  const double phiPrimeTrue = deg(60.0);
  const Vec3 tx = Q + rho * dirAt(phiPrimeTrue);
  const double sb = phiPrimeTrue + kPi;  // incident shadow boundary
  const double d = 1e-4;

  auto fieldAt = [&](double phiAngle) -> Cd {
    const Vec3 rx = Q + rho * dirAt(phiAngle);
    const WedgeGeometry g = extractWedgeGeometry(A, B, Q, tx, rx, C0);
    EXPECT_TRUE(g.valid);
    const Cd D = utdWedgeCoefficient(g.phi, g.phiPrime, kPi / 2.0, 2.0, k, rho,
                                     WedgeBoundary::Soft);
    const Cd diffracted = D * std::exp(Cd(0.0, k * rho)) / std::sqrt(rho);
    Cd incident(0.0, 0.0);
    if (g.phi - g.phiPrime < kPi)  // GO present only in the lit region
      incident = std::exp(Cd(0.0, -k * rho * std::cos(g.phi - g.phiPrime)));
    return incident + diffracted;
  };

  const Cd lit = fieldAt(sb - d);
  const Cd shadow = fieldAt(sb + d);
  EXPECT_NEAR(std::abs(lit - shadow), 0.0, 5e-3);  // no step/spike
  EXPECT_NEAR(std::abs(lit), 0.5, 3e-2);           // half field (-6 dB)
  EXPECT_NEAR(std::abs(shadow), 0.5, 3e-2);
}

// ---------------------------------------------------------------------------
// GATE 4 — monotonic shadowing
// ---------------------------------------------------------------------------

TEST(UtdWedge, MonotonicIntoShadowUnitLevel) {
  const double k = kTwoPi / 0.1;
  const double phiPrime = kPi / 2.0, beta0 = kPi / 2.0;
  const double s = 100.0, sPrime = 100.0;
  double prev = -1.0;
  // ISB at 1.5*pi; increasing delta moves the receiver deeper into shadow.
  for (double dphi : {0.02, 0.05, 0.10, 0.20, 0.40}) {
    const double loss =
        utdWedgePathLossDb(1.5 * kPi + dphi, phiPrime, beta0, 2.0, k, s, sPrime);
    EXPECT_TRUE(std::isfinite(loss));
    EXPECT_GT(loss, prev) << "dphi=" << dphi;
    prev = loss;
  }
}

// ---------------------------------------------------------------------------
// GATE 5 — wedge-angle sensitivity + determinism
// ---------------------------------------------------------------------------

TEST(UtdWedge, WedgeAngleSensitivityAndDeterminism) {
  const double k = kTwoPi / 0.1;
  const double phiPrime = deg(45.0), phi = deg(250.0), beta0 = kPi / 2.0;
  const double s = 50.0, sPrime = 50.0;
  const double half = utdWedgePathLossDb(phi, phiPrime, beta0, 2.0, k, s, sPrime);
  const double wedge = utdWedgePathLossDb(phi, phiPrime, beta0, 1.5, k, s, sPrime);
  EXPECT_TRUE(std::isfinite(half) && std::isfinite(wedge));
  EXPECT_GT(half, 0.0);
  EXPECT_GT(wedge, 0.0);
  // A sharper wedge diffracts differently — a physically meaningful, bounded gap.
  EXPECT_GT(std::abs(half - wedge), 0.1);
  EXPECT_LT(std::abs(half - wedge), 20.0);

  // Determinism: identical bits across repeated evaluation.
  EXPECT_EQ(utdWedgePathLossDb(phi, phiPrime, beta0, 1.5, k, s, sPrime), wedge);
}

// ---------------------------------------------------------------------------
// Wedge-geometry EXTRACTION unit tests
// ---------------------------------------------------------------------------

// A wall's free top edge is a half-plane -> n = 2.
TEST(UtdWedge, ExtractionFreeEdgeHalfPlane) {
  const Vec3 A{0, -5, 8}, B{0, 5, 8}, Q{0, 0, 8}, C0{0, 0, 0};  // C0 into the wall
  const Vec3 tx{-60, 0, 5}, rx{60, 0, 5};
  const WedgeGeometry g = extractWedgeGeometry(A, B, Q, tx, rx, C0);
  ASSERT_TRUE(g.valid);
  EXPECT_NEAR(g.n, 2.0, 1e-12);
  EXPECT_GT(g.phi, 0.0);
  EXPECT_LT(g.phi, kTwoPi);
  EXPECT_GT(g.phiPrime, 0.0);
  EXPECT_LT(g.phiPrime, kTwoPi);
  EXPECT_NEAR(g.beta0, kPi / 2.0, 1e-6);  // ray perpendicular to a horizontal edge
}

// A 90-degree box corner (two perpendicular faces) -> exterior 270 deg -> n = 1.5.
TEST(UtdWedge, ExtractionBoxCornerWedge) {
  const Vec3 A{0, 0, 0}, B{0, 0, 10}, Q{0, 0, 5};
  const Vec3 C0{10, 0, 0};   // face in the y = 0 plane
  const Vec3 C1{0, 10, 0};   // face in the x = 0 plane
  // tx and rx in the exterior (vacuum) reflex region, off the material quadrant.
  const Vec3 tx{-5, 2, 5}, rx{2, -5, 5};
  const WedgeGeometry g = extractWedgeGeometry(A, B, Q, tx, rx, C0, C1);
  ASSERT_TRUE(g.valid);
  EXPECT_NEAR(g.n, 1.5, 1e-6);
  const double nPi = g.n * kPi;
  EXPECT_GT(g.phi, 0.0);
  EXPECT_LT(g.phi, nPi);
  EXPECT_GT(g.phiPrime, 0.0);
  EXPECT_LT(g.phiPrime, nPi);
}

// A coplanar shared edge (mesh diagonal / flat sheet) is not a wedge -> skipped.
TEST(UtdWedge, ExtractionCoplanarSkipped) {
  const Vec3 A{0, 0, 0}, B{10, 10, 0}, Q{5, 5, 0};
  const Vec3 C0{10, 0, 0}, C1{0, 10, 0};  // both in the z = 0 plane
  const Vec3 tx{2, 8, 5}, rx{8, 2, -5};
  const WedgeGeometry g = extractWedgeGeometry(A, B, Q, tx, rx, C0, C1);
  EXPECT_FALSE(g.valid);
}

// Extracted angles swap consistently under tx<->rx (reciprocity of extraction).
TEST(UtdWedge, ExtractionAnglesReciprocal) {
  const Vec3 A{0, 0, 0}, B{0, 0, 10}, Q{0, 0, 5};
  const Vec3 C0{10, 0, 0}, C1{0, 10, 0};
  const Vec3 tx{-5, 2, 5}, rx{2, -5, 5};
  const WedgeGeometry a = extractWedgeGeometry(A, B, Q, tx, rx, C0, C1);
  const WedgeGeometry b = extractWedgeGeometry(A, B, Q, rx, tx, C0, C1);
  ASSERT_TRUE(a.valid && b.valid);
  EXPECT_NEAR(a.n, b.n, 1e-12);
  EXPECT_NEAR(a.phi, b.phiPrime, 1e-9);
  EXPECT_NEAR(a.phiPrime, b.phi, 1e-9);
  EXPECT_NEAR(a.s, b.sPrime, 1e-9);
  EXPECT_NEAR(a.sPrime, b.s, 1e-9);
}

// ---------------------------------------------------------------------------
// Scene-level gates via the Simulator (geometry-driven UTD wiring)
// ---------------------------------------------------------------------------

// Vertical wall in the plane x = 0, spanning y in [-500, 500] and z in [0, top].
// tx and rx sit on opposite sides at z = 5 so LOS is blocked and the free top
// edge diffracts. The wall is made wide so the vertical side edges never become
// the dominant (min-v) detour — the top edge stays the diffracting edge as its
// height rises, keeping the shadow-depth sweep monotone.
Scene wallScene(double top, const Vec3& txPos, const Vec3& rxPos) {
  Scene s;
  s.addMesh({Triangle{{0, -500, 0}, {0, 500, 0}, {0, 500, top}},
             Triangle{{0, -500, 0}, {0, 500, top}, {0, -500, top}}},
            "");
  Transmitter tx;
  tx.id = "tx";
  tx.position = txPos;
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43;
  s.addTransmitter(tx);
  Receiver rx;
  rx.id = "rx";
  rx.position = rxPos;
  s.addReceiver(rx);
  return s;
}

double sceneDiffractionLoss(Scene s, DiffractionModel model) {
  SimulationSettings st;
  st.maxReflections = 0;
  st.enableDiffraction = true;
  st.diffractionModel = model;
  const RFResult res = Simulator(st).run(s);
  const auto* r = res.receiver("rx");
  if (!r) return -1.0;
  for (const auto& p : r->paths)
    if (p.type == PathType::Diffraction) return p.pathLossDb;
  return -1.0;
}

// GATE 1 (scene): the geometry-driven UTD half-plane tracks the ITU-R knife edge
// across a sweep of wall heights (increasing diffraction), within ~1 dB.
TEST(UtdWedge, SceneReduceToKnifeEdge) {
  const Vec3 tx{-60, 0, 5}, rx{60, 0, 5};
  for (double top : {6.0, 7.0, 8.0, 10.0, 12.0}) {
    const double utd = sceneDiffractionLoss(wallScene(top, tx, rx),
                                            DiffractionModel::UTD);
    const double knife = sceneDiffractionLoss(wallScene(top, tx, rx),
                                              DiffractionModel::SingleEdge);
    ASSERT_GT(utd, 0.0) << "top=" << top;
    ASSERT_GT(knife, 0.0) << "top=" << top;
    EXPECT_TRUE(std::isfinite(utd));
    EXPECT_NEAR(utd, knife, 1.0) << "top=" << top;
  }
}

// GATE 2 (scene): swapping tx<->rx yields equal UTD loss.
TEST(UtdWedge, SceneReciprocity) {
  const Vec3 tx{-60, 3, 5}, rx{55, -2, 4};
  const double fwd =
      sceneDiffractionLoss(wallScene(8.0, tx, rx), DiffractionModel::UTD);
  const double rev =
      sceneDiffractionLoss(wallScene(8.0, rx, tx), DiffractionModel::UTD);
  ASSERT_GT(fwd, 0.0);
  ASSERT_GT(rev, 0.0);
  EXPECT_NEAR(fwd, rev, 1e-6);
}

// GATE 4 (scene): raising the wall drives the receiver deeper into shadow, so the
// geometry-driven UTD diffraction loss increases monotonically.
TEST(UtdWedge, SceneMonotonicIntoShadow) {
  // tx/rx at z = 25 with the wall base at z = 0: the top edge (clearance
  // top-25 <= 15) stays the dominant detour vs the constant-clearance bottom
  // edge (25), so raising the wall strictly deepens the shadow.
  const Vec3 tx{-60, 0, 25}, rx{60, 0, 25};
  double prev = -1.0;
  for (double top : {26.0, 28.0, 30.0, 35.0, 40.0}) {
    const double utd = sceneDiffractionLoss(wallScene(top, tx, rx),
                                            DiffractionModel::UTD);
    ASSERT_GT(utd, 0.0) << "top=" << top;
    EXPECT_GT(utd, prev) << "top=" << top;
    prev = utd;
  }
}

// GATE 5 (scene): a 90-degree building corner (shared non-coplanar edge) produces
// a finite, positive, deterministic wedge-diffracted path end to end.
TEST(UtdWedge, SceneBoxCornerWedgeDiffraction) {
  Scene s;
  // Two perpendicular walls sharing the vertical edge along z at the origin.
  s.addMesh(
      {// wall in x = 0, y in [-40, 0]
       Triangle{{0, -40, 0}, {0, 0, 0}, {0, 0, 30}},
       Triangle{{0, -40, 0}, {0, 0, 30}, {0, -40, 30}},
       // wall in y = 0, x in [0, 40]
       Triangle{{0, 0, 0}, {40, 0, 0}, {40, 0, 30}},
       Triangle{{0, 0, 0}, {40, 0, 30}, {0, 0, 30}}},
      "");
  Transmitter tx;
  tx.id = "tx";
  tx.position = {-30, 20, 10};  // in front of the x = 0 wall
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43;
  s.addTransmitter(tx);
  Receiver rx;
  rx.id = "rx";
  rx.position = {20, -30, 10};  // shadowed behind the corner
  s.addReceiver(rx);

  const double a = sceneDiffractionLoss(s, DiffractionModel::UTD);
  const double b = sceneDiffractionLoss(s, DiffractionModel::UTD);
  ASSERT_GT(a, 0.0) << "no UTD diffraction path formed at the corner";
  EXPECT_TRUE(std::isfinite(a));
  EXPECT_EQ(a, b);  // determinism
}

}  // namespace
