// Multi-edge diffraction (ITU-R P.526, D1): Bullington and Deygout constructions
// over a vertical-plane terrain profile, plus their integration into the
// simulator's diffracted-path finder behind SimulationSettings::diffractionModel.
//
// References are ANALYTIC: a single-obstacle profile must reduce EXACTLY to the
// single knife-edge loss knifeEdgeLossDb(v), and the two-edge cases are checked
// against hand-derived Bullington-point / Deygout sub-line clearances.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "rftrace/rf/diffraction.hpp"
#include "rftrace/rf/diffraction_multi.hpp"
#include "rftrace/rf/free_space_path_loss.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;
using rftrace::rf::bullingtonLossDb;
using rftrace::rf::deygoutLossDb;
using rftrace::rf::fresnelDiffractionParameter;
using rftrace::rf::knifeEdgeLossDb;
using rftrace::rf::ProfilePoint;
using rftrace::rf::TerrainProfile;

namespace {

constexpr double kFreq = 1.0e9;                     // 1 GHz
const double kLambda = constants::c / kFreq;        // ≈ 0.299792 m

// Single obstacle poking above a flat tx→rx line: v and both multi-edge losses
// must all equal the single knife-edge loss for that edge.
TEST(MultiEdgeDiffraction, SingleObstacleReducesToKnifeEdge) {
  TerrainProfile prof;
  prof.totalDistanceMeters = 1000.0;
  prof.txHeightMeters = 0.0;
  prof.rxHeightMeters = 0.0;
  prof.obstacles = {{400.0, 60.0}};  // interior obstacle, above the (z=0) line

  const double v =
      fresnelDiffractionParameter(60.0, 400.0, 600.0, kLambda);  // clearance=60
  const double single = knifeEdgeLossDb(v);
  ASSERT_GT(single, 0.0);  // genuinely obstructed

  EXPECT_NEAR(bullingtonLossDb(prof, kLambda), single, 1e-9);
  EXPECT_NEAR(deygoutLossDb(prof, kLambda), single, 1e-9);
}

// The reduction holds with unequal endpoint heights too (Bullington point still
// coincides with the lone obstacle; clearance is measured from the sloped line).
TEST(MultiEdgeDiffraction, SingleObstacleReducesWithSlopedEndpoints) {
  TerrainProfile prof;
  prof.totalDistanceMeters = 1000.0;
  prof.txHeightMeters = 10.0;
  prof.rxHeightMeters = 30.0;
  prof.obstacles = {{300.0, 80.0}};

  const double lineHeight = 10.0 + (30.0 - 10.0) * (300.0 / 1000.0);  // = 16
  const double clearance = 80.0 - lineHeight;                         // = 64
  const double v = fresnelDiffractionParameter(clearance, 300.0, 700.0, kLambda);
  const double single = knifeEdgeLossDb(v);
  ASSERT_GT(single, 0.0);

  EXPECT_NEAR(bullingtonLossDb(prof, kLambda), single, 1e-9);
  EXPECT_NEAR(deygoutLossDb(prof, kLambda), single, 1e-9);
}

// Two equal edges → both methods exceed the single dominant edge and match a
// hand-derived reference (Bullington point at mid-span; Deygout main + rx-side).
TEST(MultiEdgeDiffraction, TwoEqualEdgesVersusHandReference) {
  TerrainProfile prof;
  prof.totalDistanceMeters = 1000.0;
  prof.txHeightMeters = 0.0;
  prof.rxHeightMeters = 0.0;
  prof.obstacles = {{300.0, 50.0}, {700.0, 50.0}};

  // Single dominant edge alone (edge at 300 relative to the z=0 direct line).
  const double vMain = fresnelDiffractionParameter(50.0, 300.0, 700.0, kLambda);
  const double singleEdgeLoss = knifeEdgeLossDb(vMain);

  // --- Bullington reference ------------------------------------------------
  // slopeTx = 50/300, slopeRx = 50/300 (symmetric) → db = 500, hb = 83.3333.
  const double slope = 50.0 / 300.0;
  const double db = 500.0;
  const double hb = slope * db;  // 83.3333…
  const double vBul = fresnelDiffractionParameter(hb, db, 1000.0 - db, kLambda);
  const double bullingtonRef = knifeEdgeLossDb(vBul);

  EXPECT_NEAR(bullingtonLossDb(prof, kLambda), bullingtonRef, 1e-9);
  EXPECT_GT(bullingtonLossDb(prof, kLambda), singleEdgeLoss);

  // --- Deygout reference ---------------------------------------------------
  // Main = edge at 300 (ties resolve to the first). rx-side sub-line joins
  // (300,50)→(1000,0); the edge at 700 has span 700, d1=400, d2=300.
  const double subLineHeight = 50.0 + (0.0 - 50.0) * (400.0 / 700.0);  // 21.4286
  const double subClearance = 50.0 - subLineHeight;                    // 28.5714
  const double vRx = fresnelDiffractionParameter(subClearance, 400.0, 300.0, kLambda);
  const double deygoutRef = knifeEdgeLossDb(vMain) + knifeEdgeLossDb(vRx);

  EXPECT_NEAR(deygoutLossDb(prof, kLambda), deygoutRef, 1e-9);
  EXPECT_GT(deygoutLossDb(prof, kLambda), singleEdgeLoss);
}

// Raising the obstacles deeper into the shadow increases both losses monotonically.
TEST(MultiEdgeDiffraction, LossGrowsWithObstacleHeight) {
  const double heights[] = {20.0, 40.0, 60.0, 80.0, 120.0};
  double prevB = -1.0, prevD = -1.0;
  for (const double h : heights) {
    TerrainProfile prof;
    prof.totalDistanceMeters = 1000.0;
    prof.obstacles = {{300.0, h}, {700.0, h}};
    const double lb = bullingtonLossDb(prof, kLambda);
    const double ld = deygoutLossDb(prof, kLambda);
    EXPECT_GT(lb, prevB) << "h=" << h;
    EXPECT_GT(ld, prevD) << "h=" << h;
    prevB = lb;
    prevD = ld;
  }
}

// A second obstacle adds loss on top of a single edge (Deygout is additive).
TEST(MultiEdgeDiffraction, SecondObstacleAddsLoss) {
  TerrainProfile one;
  one.totalDistanceMeters = 1000.0;
  one.obstacles = {{300.0, 50.0}};

  TerrainProfile two = one;
  two.obstacles.push_back({700.0, 50.0});

  EXPECT_GT(bullingtonLossDb(two, kLambda), bullingtonLossDb(one, kLambda));
  EXPECT_GT(deygoutLossDb(two, kLambda), deygoutLossDb(one, kLambda));
}

// Empty profile → exactly 0 dB; obstacles well below the direct line → ~0 dB.
TEST(MultiEdgeDiffraction, ClearProfileIsZero) {
  TerrainProfile empty;
  empty.totalDistanceMeters = 1000.0;
  EXPECT_EQ(bullingtonLossDb(empty, kLambda), 0.0);
  EXPECT_EQ(deygoutLossDb(empty, kLambda), 0.0);

  TerrainProfile clear;
  clear.totalDistanceMeters = 1000.0;
  clear.txHeightMeters = 50.0;
  clear.rxHeightMeters = 50.0;
  clear.obstacles = {{300.0, 0.0}, {700.0, 0.0}};  // far below the z=50 line
  EXPECT_NEAR(bullingtonLossDb(clear, kLambda), 0.0, 1e-12);
  EXPECT_NEAR(deygoutLossDb(clear, kLambda), 0.0, 1e-12);
}

// ---------------------------------------------------------------------------
// Integration: a shadowed link builds a profile and applies the selected model.
// ---------------------------------------------------------------------------

Transmitter mkTx(const Vec3& p) {
  Transmitter t;
  t.id = "tx";
  t.position = p;
  t.frequencyHz = 1.0e9;
  t.powerDbm = 40.0;
  return t;
}

Receiver mkRx(const Vec3& p) {
  Receiver r;
  r.id = "rx";
  r.position = p;
  return r;
}

// A slanted concrete wall (a single open quad with x-extent) so downward
// profile rays reliably hit its surface: x∈[-4,4], rising from z=0 at x=-4 to
// z=20 at x=4, spanning y∈[-200,200]. It blocks the tx(-50,0,5)→rx(50,0,5) LOS
// and exposes a top boundary edge above the line for the edge finder.
Scene slantedWallScene() {
  Scene s;
  s.addMaterial(materials::preset("concrete"));
  const Vec3 a{-4, -200, 0}, b{-4, 200, 0}, c{4, 200, 20}, d{4, -200, 20};
  s.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  s.addTransmitter(mkTx({-50, 0, 5}));
  s.addReceiver(mkRx({50, 0, 5}));
  return s;
}

const RFPath* firstDiffraction(const ReceiverResult& rr) {
  for (const RFPath& p : rr.paths)
    if (p.type == PathType::Diffraction) return &p;
  return nullptr;
}

TEST(MultiEdgeDiffraction, MultiEdgeModelProducesDiffractedPath) {
  Scene s = slantedWallScene();

  for (const DiffractionModel model :
       {DiffractionModel::Bullington, DiffractionModel::Deygout}) {
    SimulationSettings st;
    st.maxReflections = 0;
    st.enableDiffraction = true;
    st.diffractionModel = model;
    const RFResult r = Simulator(st).run(s);
    const ReceiverResult* rr = r.receiver("rx");
    ASSERT_NE(rr, nullptr);
    const RFPath* dif = firstDiffraction(*rr);
    ASSERT_NE(dif, nullptr) << "model=" << static_cast<int>(model);
    EXPECT_EQ(dif->diffractions, 1);
    // A genuinely shadowed link: the diffraction loss is positive, so the path
    // loss exceeds free-space over the diffracted length.
    const double len = (dif->points[1] - dif->points[0]).norm() +
                       (dif->points[2] - dif->points[1]).norm();
    EXPECT_GT(dif->pathLossDb, rf::freeSpacePathLossDb(len, kFreq) + 1.0);
  }
}

// Default (SingleEdge) diffraction is bit-for-bit the archived single knife-edge
// path: selecting a multi-edge model must not perturb the default result.
TEST(MultiEdgeDiffraction, DefaultModelUnchanged) {
  Scene s = slantedWallScene();

  SimulationSettings def;  // diffractionModel defaults to SingleEdge
  def.maxReflections = 0;
  def.enableDiffraction = true;
  ASSERT_EQ(def.diffractionModel, DiffractionModel::SingleEdge);

  const RFResult r = Simulator(def).run(s);
  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  const RFPath* dif = firstDiffraction(*rr);
  ASSERT_NE(dif, nullptr);

  // Recompute the single dominant knife-edge loss for the found detour point and
  // confirm the default path budget matches it exactly (FSPL + J(v)).
  const auto g = rf::diffractionGeometry(dif->points[0], dif->points[2],
                                         dif->points[1]);
  const double v = fresnelDiffractionParameter(g.clearanceMeters, g.d1, g.d2,
                                               kLambda);
  const double len = (dif->points[1] - dif->points[0]).norm() +
                     (dif->points[2] - dif->points[1]).norm();
  const double expected = rf::freeSpacePathLossDb(len, kFreq) + knifeEdgeLossDb(v);
  EXPECT_NEAR(dif->pathLossDb, expected, 1e-9);
}

}  // namespace
