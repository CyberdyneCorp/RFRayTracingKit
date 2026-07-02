// Single knife-edge diffraction (ITU-R P.526, Phase 7 / R2): the J(v) loss
// formula reference points, and the simulator's diffracted-path generation
// behind the default-off enableDiffraction flag.

#include <gtest/gtest.h>

#include <cmath>
#include <iterator>

#include "rftrace/rf/diffraction.hpp"
#include "rftrace/rf/free_space_path_loss.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;
using rftrace::rf::diffractionGeometry;
using rftrace::rf::fresnelDiffractionParameter;
using rftrace::rf::knifeEdgeLossDb;

namespace {

Transmitter mkTx(const Vec3& p) {
  Transmitter t;
  t.id = "tx";
  t.position = p;
  t.frequencyHz = 3.5e9;
  t.powerDbm = 43.0;
  return t;
}

Receiver mkRx(const Vec3& p) {
  Receiver r;
  r.id = "rx";
  r.position = p;
  return r;
}

// A wide concrete wall in the plane x=0 (y ∈ [-200,200], z ∈ [0,10]) that blocks
// the LOS between a tx at (-50,0,5) and an rx at (50,0,5) — both at z=5, inside
// the wall's height span. The wall is far wider than tall, so the dominant
// (least-loss) silhouette edge is the top/bottom horizontal edge, not a side.
Scene shadowedWallScene() {
  Scene s;
  s.addMaterial(materials::preset("concrete"));
  const Vec3 a{0, -200, 0}, b{0, 200, 0}, c{0, 200, 10}, d{0, -200, 10};
  s.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  s.addTransmitter(mkTx({-50, 0, 5}));
  s.addReceiver(mkRx({50, 0, 5}));
  return s;
}

}  // namespace

// J(0) ≈ 6 dB: the ray grazing the edge loses ~6 dB (ITU-R P.526 gives 6.03).
TEST(Diffraction, GrazingEdgeIsSixDb) {
  EXPECT_NEAR(knifeEdgeLossDb(0.0), 6.0, 0.1);
}

// Loss increases monotonically as the receiver moves deeper into the shadow.
TEST(Diffraction, MonotonicIntoShadow) {
  const double vs[] = {-0.5, 0.0, 0.5, 1.0, 2.0, 3.0, 5.0};
  double prev = knifeEdgeLossDb(vs[0]);
  for (std::size_t i = 1; i < std::size(vs); ++i) {
    const double cur = knifeEdgeLossDb(vs[i]);
    EXPECT_GT(cur, prev) << "v=" << vs[i];
    prev = cur;
  }
}

// Ample Fresnel clearance (strongly negative v) → ~0 dB, clamped exactly to 0.
TEST(Diffraction, ClearLineOfSightIsZero) {
  EXPECT_NEAR(knifeEdgeLossDb(-3.0), 0.0, 1e-12);
  EXPECT_NEAR(knifeEdgeLossDb(-1.0), 0.0, 1e-12);
}

// A shadowed receiver gains exactly one diffracted path whose loss is FSPL over
// the diffracted length plus the P.526 knife-edge loss for that edge.
TEST(Diffraction, ShadowedReceiverGainsDiffractedPath) {
  Scene s = shadowedWallScene();

  SimulationSettings st;
  st.maxReflections = 0;  // isolate the diffracted contribution
  st.enableDiffraction = true;
  const RFResult r = Simulator(st).run(s);

  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  ASSERT_EQ(rr->paths.size(), 1u);

  const RFPath& p = rr->paths[0];
  EXPECT_EQ(p.type, PathType::Diffraction);
  EXPECT_EQ(p.diffractions, 1);
  ASSERT_EQ(p.points.size(), 3u);

  const double d1 = (p.points[1] - p.points[0]).norm();
  const double d2 = (p.points[2] - p.points[1]).norm();
  const double lambda = constants::c / 3.5e9;
  const auto g = diffractionGeometry(p.points[0], p.points[2], p.points[1]);
  const double v =
      fresnelDiffractionParameter(g.clearanceMeters, g.d1, g.d2, lambda);
  const double kedge = knifeEdgeLossDb(v);
  const double expectedLoss = rf::freeSpacePathLossDb(d1 + d2, 3.5e9) + kedge;

  EXPECT_GT(kedge, 0.0);  // genuinely shadowed, not grazing
  EXPECT_NEAR(p.pathLossDb, expectedLoss, 1e-9);
  EXPECT_NEAR(p.receivedPowerDbm, 43.0 - expectedLoss, 1e-9);
}

// With diffraction disabled (the default), no diffraction path is produced and
// the blocked receiver keeps its Phase 1/2 behavior (LOS blocked → no signal).
TEST(Diffraction, DisabledProducesNoDiffractedPath) {
  Scene s = shadowedWallScene();

  SimulationSettings st;
  st.maxReflections = 1;
  st.enableDiffraction = false;
  const RFResult r = Simulator(st).run(s);

  const ReceiverResult* rr = r.receiver("rx");
  ASSERT_NE(rr, nullptr);
  for (const RFPath& p : rr->paths)
    EXPECT_NE(p.type, PathType::Diffraction);
}
