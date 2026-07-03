// Groundwork + Phase 1 tests for the batched simulator path.
//
// Part A exercises detail::BatchQuery directly: gathered order preserved and
// results index-aligned against the CPU backend, buffer reuse across repeated
// clear()/add()/run cycles, empty-batch no-op, and runClosestHit alignment.
//
// Part B guards the batched LOS scatter in Simulator::run / runCoverage: a mixed
// occluder + diffraction scene must be bit-identical across repeated runs, and
// known-clear vs known-blocked receivers must resolve to LOS vs diffraction (so
// a mis-stepped scatter counter would be caught).

#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "rftrace/backend.hpp"
#include "rftrace/coverage.hpp"
#include "rftrace/detail/batch_query.hpp"
#include "rftrace/geometry.hpp"
#include "rftrace/math.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {

// A wall in the x=5 plane spanning y,z in [0,4], split by the diagonal.
std::vector<Triangle> makeWall() {
  return {
      {Vec3(5, 0, 0), Vec3(5, 4, 0), Vec3(5, 0, 4)},
      {Vec3(5, 4, 0), Vec3(5, 4, 4), Vec3(5, 0, 4)},
  };
}

// Rays crossing the x=5 wall: some blocked, some clear.
std::vector<Ray> makeSegments() {
  return {
      segmentRay(Vec3(0, 1, 1), Vec3(10, 1, 1)),    // through the wall: blocked
      segmentRay(Vec3(0, 1, 1), Vec3(4, 1, 1)),     // stops before wall: clear
      segmentRay(Vec3(0, 3, 3), Vec3(10, 3, 3)),    // through the wall: blocked
      segmentRay(Vec3(0, -9, 0), Vec3(0, -9, 10)),  // off to the side: clear
  };
}

Transmitter makeTx(const Vec3& p, const std::string& id = "tx") {
  Transmitter tx;
  tx.id = id;
  tx.position = p;
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  return tx;
}
Receiver makeRx(const Vec3& p, const std::string& id) {
  Receiver rx;
  rx.id = id;
  rx.position = p;
  return rx;
}

// A tall wall in y=100 spanning x in [0,300], z in [0,50]; a knife edge for
// receivers placed behind it below its top.
void addBlockingWall(Scene& scene) {
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "");
}

}  // namespace

// --- Part A: BatchQuery unit tests -----------------------------------------

TEST(BatchQuery, GatheredOrderIndexAligned) {
  auto cpu = makeBackend(Backend::CPU, false);
  cpu->build(makeWall());
  const std::vector<Ray> rays = makeSegments();

  detail::BatchQuery q;
  std::vector<std::size_t> tokens;
  for (const Ray& r : rays) tokens.push_back(q.add(r));
  ASSERT_EQ(q.size(), rays.size());
  q.runOcclusion(*cpu);

  for (std::size_t i = 0; i < rays.size(); ++i)
    EXPECT_EQ(q.occluded(tokens[i]), cpu->occluded(rays[i])) << "ray " << i;
}

TEST(BatchQuery, BuffersReusedAcrossCycles) {
  auto cpu = makeBackend(Backend::CPU, false);
  cpu->build(makeWall());
  const std::vector<Ray> rays = makeSegments();

  detail::BatchQuery q;
  for (int cycle = 0; cycle < 3; ++cycle) {
    q.clear();
    EXPECT_TRUE(q.empty());
    // Vary the gather size each cycle to force buffer growth/reuse.
    const std::size_t n = rays.size() - static_cast<std::size_t>(cycle);
    std::vector<std::size_t> tokens;
    for (std::size_t i = 0; i < n; ++i) tokens.push_back(q.add(rays[i]));
    q.runOcclusion(*cpu);
    for (std::size_t i = 0; i < n; ++i)
      EXPECT_EQ(q.occluded(tokens[i]), cpu->occluded(rays[i]))
          << "cycle " << cycle << " ray " << i;
  }
}

TEST(BatchQuery, EmptyBatchIsNoOp) {
  auto cpu = makeBackend(Backend::CPU, false);
  cpu->build(makeWall());

  detail::BatchQuery q;
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(q.size(), 0u);
  EXPECT_NO_THROW(q.runOcclusion(*cpu));
  EXPECT_NO_THROW(q.runClosestHit(*cpu));
}

TEST(BatchQuery, ClosestHitIndexAligned) {
  auto cpu = makeBackend(Backend::CPU, false);
  cpu->build(makeWall());
  const std::vector<Ray> rays = makeSegments();

  detail::BatchQuery q;
  std::vector<std::size_t> tokens;
  for (const Ray& r : rays) tokens.push_back(q.add(r));
  q.runClosestHit(*cpu);

  for (std::size_t i = 0; i < rays.size(); ++i) {
    const Hit expected = cpu->closestHit(rays[i]);
    EXPECT_EQ(q.hit(tokens[i]).valid, expected.valid) << "ray " << i;
    EXPECT_EQ(q.hit(tokens[i]).triangle, expected.triangle) << "ray " << i;
    EXPECT_DOUBLE_EQ(q.hit(tokens[i]).t, expected.t) << "ray " << i;
  }
}

// --- Part B: batched Simulator equality / determinism ----------------------

TEST(BatchedSim, RunIsDeterministicWithMixedOccluderAndDiffraction) {
  Scene scene;
  addBlockingWall(scene);
  scene.addTransmitter(makeTx({150, 50, 20}));
  // rx_clear: same side as tx, LOS unobstructed.
  scene.addReceiver(makeRx({150, 60, 20}, "rx_clear"));
  // rx_blocked: behind the wall and below its top -> LOS blocked -> diffraction.
  scene.addReceiver(makeRx({150, 150, 5}, "rx_blocked"));

  SimulationSettings s;
  s.maxReflections = 0;
  s.enableDiffraction = true;

  const RFResult a = Simulator(s).run(scene);
  const RFResult b = Simulator(s).run(scene);

  ASSERT_EQ(a.receivers.size(), 2u);
  ASSERT_EQ(a.receivers.size(), b.receivers.size());
  for (std::size_t i = 0; i < a.receivers.size(); ++i) {
    const ReceiverResult& ra = a.receivers[i];
    const ReceiverResult& rb = b.receivers[i];
    EXPECT_EQ(ra.paths.size(), rb.paths.size());
    EXPECT_DOUBLE_EQ(ra.receivedPowerDbm, rb.receivedPowerDbm);
    EXPECT_DOUBLE_EQ(ra.pathLossDb, rb.pathLossDb);
  }

  // Scatter ordering: the clear receiver resolves to LOS, the blocked one to a
  // diffracted detour.
  const ReceiverResult* clear = a.receiver("rx_clear");
  const ReceiverResult* blocked = a.receiver("rx_blocked");
  ASSERT_NE(clear, nullptr);
  ASSERT_NE(blocked, nullptr);
  ASSERT_EQ(clear->paths.size(), 1u);
  EXPECT_EQ(clear->paths[0].type, PathType::LOS);
  ASSERT_EQ(blocked->paths.size(), 1u);
  EXPECT_EQ(blocked->paths[0].type, PathType::Diffraction);
}

TEST(BatchedSim, CoverageIsDeterministicAndPartiallyBlocked) {
  Scene scene;
  addBlockingWall(scene);
  scene.addTransmitter(makeTx({150, 50, 20}));

  CoverageGrid grid;
  grid.origin = Vec3{100, 40, 5};
  grid.cellSize = 20.0;
  grid.cols = 4;
  grid.rows = 8;  // spans y from 40 to ~200, i.e. across the wall at y=100

  SimulationSettings s;
  s.maxReflections = 0;

  const CoverageResult a = Simulator(s).runCoverage(scene, grid);
  const CoverageResult b = Simulator(s).runCoverage(scene, grid);

  ASSERT_EQ(a.powerDbm.size(), b.powerDbm.size());
  bool anyCovered = false, anyNoSignal = false;
  for (std::size_t i = 0; i < a.powerDbm.size(); ++i) {
    EXPECT_DOUBLE_EQ(a.powerDbm[i], b.powerDbm[i]) << "cell " << i;
    EXPECT_DOUBLE_EQ(a.pathLossDb[i], b.pathLossDb[i]) << "cell " << i;
    if (a.powerDbm[i] == CoverageResult::NoSignal)
      anyNoSignal = true;
    else
      anyCovered = true;
  }
  // The grid straddles the wall, so the batched scatter must yield both covered
  // (near, clear) and no-signal (behind the wall) cells.
  EXPECT_TRUE(anyCovered);
  EXPECT_TRUE(anyNoSignal);
}
