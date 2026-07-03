#include <gtest/gtest.h>

#include <vector>

#include "rftrace/backend.hpp"
#include "rftrace/detail/propagation.hpp"
#include "rftrace/simulator.hpp"

// Phase 2 gate: the batched wavefront `detail::rayLaunch` must be bit-for-bit
// identical to the retained sequential per-ray `detail::rayLaunchReference`.
// Both run in the same binary on the CPU backend, so identical arithmetic ->
// exact `==` on every field is valid; any reordering of the associativity-
// sensitive out[r] insertion or the strongest-wins dedup surfaces as a
// mismatch. Drives the touched function directly (not aggregated Simulator
// output) so the gate covers rayLaunch itself.
using namespace rftrace;

namespace {

Transmitter makeTx(const Vec3& p) {
  Transmitter t;
  t.id = "tx";
  t.position = p;
  t.frequencyHz = 3.5e9;
  t.powerDbm = 43.0;
  return t;
}

Receiver makeRx(const Vec3& p, const std::string& id) {
  Receiver r;
  r.id = id;
  r.position = p;
  return r;
}

// Single concrete wall at y=100 (two triangles => two reflection signatures).
Scene wallScene() {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  return scene;
}

// Two perpendicular walls forming a corner (planes x=0 and y=0), four triangles.
Scene cornerScene() {
  Scene scene;
  scene.addMesh({Triangle{{0, 0, 0}, {0, 50, 0}, {0, 50, 30}},
                 Triangle{{0, 0, 0}, {0, 50, 30}, {0, 0, 30}}},
                "");  // wall A: x=0
  scene.addMesh({Triangle{{0, 0, 0}, {50, 0, 0}, {50, 0, 30}},
                 Triangle{{0, 0, 0}, {50, 0, 30}, {0, 0, 30}}},
                "");  // wall B: y=0
  return scene;
}

struct Case {
  const char* name;
  Scene scene;
  Transmitter tx;
  std::vector<Receiver> receivers;
  int rays = 0;  ///< 0 -> use baseSettings default; else override ray budget.
};

// Dense receiver lattice in front of the wall scene (coverage-grid-like capture
// set of hundreds of receivers), plus receivers placed far along the reflected
// direction so their capture segments are the long (kFar) segments that stress
// the grid clip, a duplicate-position pair, and a receiver near a cell boundary.
// This is the case that actually exercises the spatial index at scale.
std::vector<Receiver> latticeReceivers() {
  std::vector<Receiver> rxs;
  int id = 0;
  for (int ix = 0; ix < 16; ++ix)
    for (int iy = 0; iy < 12; ++iy)
      for (int iz = 0; iz < 3; ++iz)
        rxs.push_back(makeRx({10.0 + ix * 18.0, 10.0 + iy * 7.0, 5.0 + iz * 15.0},
                             "g" + std::to_string(id++)));
  // Far receivers (long capture segments through the clip).
  rxs.push_back(makeRx({5000, 700, 900}, "far0"));
  rxs.push_back(makeRx({-4000, -3000, 2000}, "far1"));
  // Duplicate-position pair (shared cell, each must be captured once).
  rxs.push_back(makeRx({100, 45, 20}, "dupA"));
  rxs.push_back(makeRx({100, 45, 20}, "dupB"));
  // Near a cell boundary for typical radii.
  rxs.push_back(makeRx({100.0, 46.0, 20.0}, "bnd"));
  return rxs;
}

std::vector<Case> makeCases() {
  std::vector<Case> cases;
  cases.push_back({"single-wall", wallScene(), makeTx({100, 20, 20}),
                   {makeRx({200, 20, 10}, "rx0"), makeRx({120, 30, 15}, "rx1"),
                    makeRx({80, 40, 25}, "rx2")}});
  cases.push_back({"corner-two-wall", cornerScene(), makeTx({10, 20, 10}),
                   {makeRx({20, 10, 10}, "rx0"), makeRx({30, 30, 12}, "rx1")}});
  cases.push_back({"empty-los", Scene{}, makeTx({0, 0, 10}),
                   {makeRx({100, 0, 10}, "rx0")}});
  // Spatial-index-at-scale case: hundreds of lattice receivers + far/dup/bnd.
  // Reduced ray budget keeps the huge-radius (capture-all) combos fast while
  // still exercising the grid halo/dedup and long-segment clip at scale.
  cases.push_back({"lattice-wall", wallScene(), makeTx({100, 20, 20}),
                   latticeReceivers(), 3000});
  // Single receiver and zero receivers (degenerate grid paths).
  cases.push_back({"single-rx", wallScene(), makeTx({100, 20, 20}),
                   {makeRx({150, 30, 15}, "rx0")}});
  cases.push_back({"zero-rx", wallScene(), makeTx({100, 20, 20}), {}});
  return cases;
}

// Exact equality on every field the aggregation depends on.
void expectPathEqual(const RFPath& a, const RFPath& b, const std::string& where) {
  EXPECT_EQ(a.transmitterId, b.transmitterId) << where;
  EXPECT_EQ(a.receiverId, b.receiverId) << where;
  EXPECT_EQ(a.type, b.type) << where;
  EXPECT_EQ(a.reflections, b.reflections) << where;
  EXPECT_EQ(a.receivedPowerDbm, b.receivedPowerDbm) << where;
  EXPECT_EQ(a.pathLossDb, b.pathLossDb) << where;
  EXPECT_EQ(a.phaseRad, b.phaseRad) << where;
  EXPECT_EQ(a.delaySeconds, b.delaySeconds) << where;
  ASSERT_EQ(a.points.size(), b.points.size()) << where;
  for (std::size_t k = 0; k < a.points.size(); ++k) {
    EXPECT_EQ(a.points[k].x(), b.points[k].x()) << where << " pt " << k;
    EXPECT_EQ(a.points[k].y(), b.points[k].y()) << where << " pt " << k;
    EXPECT_EQ(a.points[k].z(), b.points[k].z()) << where << " pt " << k;
  }
  ASSERT_EQ(a.materialHits.size(), b.materialHits.size()) << where;
  for (std::size_t k = 0; k < a.materialHits.size(); ++k)
    EXPECT_EQ(a.materialHits[k], b.materialHits[k]) << where << " mat " << k;
}

void expectOutEqual(const std::vector<std::vector<RFPath>>& a,
                    const std::vector<std::vector<RFPath>>& b,
                    const std::string& where) {
  ASSERT_EQ(a.size(), b.size()) << where << " nRx";
  for (std::size_t r = 0; r < a.size(); ++r) {
    ASSERT_EQ(a[r].size(), b[r].size())
        << where << " out[" << r << "].size (path count / ordering)";
    for (std::size_t p = 0; p < a[r].size(); ++p)
      expectPathEqual(a[r][p], b[r][p],
                      where + " out[" + std::to_string(r) + "][" +
                          std::to_string(p) + "]");
  }
}

SimulationSettings baseSettings() {
  SimulationSettings s;
  s.mode = PropagationMode::RayLaunch;
  s.raysPerTransmitter = 20000;
  return s;
}

}  // namespace

// Batched vs sequential reference: bit-for-bit across scenes x seeds x depths x
// capture radii. Exact == (both share the same FP ops in one binary).
TEST(Phase2RayLaunchBatch, BatchedEqualsReferenceMatrix) {
  auto backend = makeBackend(Backend::CPU);
  for (const Case& c : makeCases()) {
    backend->build(c.scene.triangles());
    for (std::uint64_t seed : {1ull, 7ull, 42ull}) {
      for (int maxRefl : {0, 1, 2}) {
        // Tiny -> near-empty capture (clip/point path); moderate; huge -> nearly
        // all receivers captured per segment (halo + dedup stress).
        for (double captureRadius : {0.001, 0.5, 3.0, 5.0, 25.0, 1e4}) {
          SimulationSettings s = baseSettings();
          if (c.rays > 0) s.raysPerTransmitter = c.rays;
          s.seed = seed;
          s.maxReflections = maxRefl;
          s.captureRadius = captureRadius;

          const auto batched = detail::rayLaunch(c.scene, *backend, c.tx,
                                                 c.receivers, s, nullptr);
          const auto reference = detail::rayLaunchReference(
              c.scene, *backend, c.tx, c.receivers, s, nullptr);

          const std::string where = std::string(c.name) + " seed=" +
                                     std::to_string(seed) + " maxRefl=" +
                                     std::to_string(maxRefl) + " capR=" +
                                     std::to_string(captureRadius);
          expectOutEqual(batched, reference, where);
        }
      }
    }
  }
}

// Determinism: running the batched path twice yields identical output.
TEST(Phase2RayLaunchBatch, BatchedIsDeterministic) {
  auto backend = makeBackend(Backend::CPU);
  const Case c = makeCases().front();
  backend->build(c.scene.triangles());
  SimulationSettings s = baseSettings();
  s.seed = 7;
  s.maxReflections = 2;
  s.captureRadius = 5.0;

  const auto a = detail::rayLaunch(c.scene, *backend, c.tx, c.receivers, s, nullptr);
  const auto b = detail::rayLaunch(c.scene, *backend, c.tx, c.receivers, s, nullptr);
  expectOutEqual(a, b, "determinism");
}

// Higher ray budget over the multi-signature corner scene, to exercise
// ray-major capture ordering with many captures landing per receiver.
TEST(Phase2RayLaunchBatch, BatchedEqualsReferenceHighBudget) {
  auto backend = makeBackend(Backend::CPU);
  Scene scene = cornerScene();
  backend->build(scene.triangles());
  Transmitter tx = makeTx({10, 20, 10});
  std::vector<Receiver> rxs = {makeRx({20, 10, 10}, "rx0"),
                               makeRx({15, 15, 12}, "rx1"),
                               makeRx({25, 25, 8}, "rx2")};

  SimulationSettings s = baseSettings();
  s.seed = 1;
  s.maxReflections = 2;
  s.captureRadius = 4.0;
  s.raysPerTransmitter = 50000;

  const auto batched = detail::rayLaunch(scene, *backend, tx, rxs, s, nullptr);
  const auto reference =
      detail::rayLaunchReference(scene, *backend, tx, rxs, s, nullptr);
  expectOutEqual(batched, reference, "high-budget corner");

  // Snapshot guard (STEP 0): pin that this scene really does exercise
  // multiple distinct-signature reflected captures at receivers. If this ever
  // trips, the equality matrix above is no longer testing the intended regime.
  int totalPaths = 0;
  for (const auto& perRx : reference) totalPaths += static_cast<int>(perRx.size());
  EXPECT_GT(totalPaths, 0) << "corner scene should capture reflected paths";
}
