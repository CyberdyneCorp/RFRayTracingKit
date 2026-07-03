#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "rftrace/route.hpp"
#include "rftrace/simulator.hpp"

// Phase 3 (cpu-full-run-acceleration): the Simulator caches its built
// acceleration backend keyed by a content hash of the scene geometry (triangle
// bytes + count). Repeated runs on an unchanged scene must REUSE the backend
// (backendRebuildCount() stays 1) and produce results BIT-FOR-BIT identical to
// building the backend per call. Any geometry change — a different triangle
// count OR the same count with different coordinates — must invalidate the cache
// and rebuild. The golden suites pin absolute numeric values; here we assert
// reuse, invalidation, and reused-vs-fresh equality.

using namespace rftrace;

namespace {

// NaN-safe exact equality over raw IEEE-754 bits (== is always false for NaN).
bool bitsEqual(double a, double b) {
  std::uint64_t ua, ub;
  std::memcpy(&ua, &a, sizeof ua);
  std::memcpy(&ub, &b, sizeof ub);
  return ua == ub;
}

bool bitsEqual(const Vec3& a, const Vec3& b) {
  return bitsEqual(a.x(), b.x()) && bitsEqual(a.y(), b.y()) &&
         bitsEqual(a.z(), b.z());
}

bool bitsEqual(const std::vector<double>& a, const std::vector<double>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (!bitsEqual(a[i], b[i])) return false;
  return true;
}

void expectPathEqual(const RFPath& a, const RFPath& b) {
  EXPECT_EQ(a.transmitterId, b.transmitterId);
  EXPECT_EQ(a.receiverId, b.receiverId);
  EXPECT_EQ(a.type, b.type);
  EXPECT_EQ(a.reflections, b.reflections);
  EXPECT_EQ(a.diffractions, b.diffractions);
  EXPECT_EQ(a.materialHits, b.materialHits);
  EXPECT_TRUE(bitsEqual(a.receivedPowerDbm, b.receivedPowerDbm));
  EXPECT_TRUE(bitsEqual(a.pathLossDb, b.pathLossDb));
  EXPECT_TRUE(bitsEqual(a.phaseRad, b.phaseRad));
  EXPECT_TRUE(bitsEqual(a.delaySeconds, b.delaySeconds));
  EXPECT_TRUE(bitsEqual(a.dopplerHz, b.dopplerHz));
  ASSERT_EQ(a.points.size(), b.points.size());
  for (std::size_t i = 0; i < a.points.size(); ++i)
    EXPECT_TRUE(bitsEqual(a.points[i], b.points[i]));
}

void expectReceiverEqual(const ReceiverResult& a, const ReceiverResult& b) {
  EXPECT_EQ(a.receiverId, b.receiverId);
  EXPECT_EQ(a.hasSignal, b.hasSignal);
  EXPECT_TRUE(bitsEqual(a.receivedPowerDbm, b.receivedPowerDbm));
  EXPECT_TRUE(bitsEqual(a.pathLossDb, b.pathLossDb));
  EXPECT_TRUE(bitsEqual(a.delaySpreadNs, b.delaySpreadNs));
  EXPECT_TRUE(bitsEqual(a.phaseRad, b.phaseRad));
  EXPECT_TRUE(bitsEqual(a.sinrDb, b.sinrDb));
  EXPECT_TRUE(bitsEqual(a.interferencePowerDbm, b.interferencePowerDbm));
  EXPECT_EQ(a.servingTransmitterId, b.servingTransmitterId);
  ASSERT_EQ(a.paths.size(), b.paths.size());
  for (std::size_t i = 0; i < a.paths.size(); ++i)
    expectPathEqual(a.paths[i], b.paths[i]);
}

void expectResultEqual(const RFResult& a, const RFResult& b) {
  ASSERT_EQ(a.receivers.size(), b.receivers.size());
  for (std::size_t i = 0; i < a.receivers.size(); ++i)
    expectReceiverEqual(a.receivers[i], b.receivers[i]);
}

void expectCoverageEqual(const CoverageResult& a, const CoverageResult& b) {
  EXPECT_TRUE(bitsEqual(a.powerDbm, b.powerDbm));
  EXPECT_TRUE(bitsEqual(a.pathLossDb, b.pathLossDb));
  EXPECT_TRUE(bitsEqual(a.sinrDb, b.sinrDb));
}

void expectRouteEqual(const RouteResult& a, const RouteResult& b) {
  ASSERT_EQ(a.samples.size(), b.samples.size());
  for (std::size_t i = 0; i < a.samples.size(); ++i) {
    const RouteSample& x = a.samples[i];
    const RouteSample& y = b.samples[i];
    EXPECT_EQ(x.index, y.index);
    EXPECT_TRUE(bitsEqual(x.distanceMeters, y.distanceMeters));
    EXPECT_TRUE(bitsEqual(x.position, y.position));
    EXPECT_EQ(x.hasSignal, y.hasSignal);
    EXPECT_TRUE(bitsEqual(x.receivedPowerDbm, y.receivedPowerDbm));
    EXPECT_TRUE(bitsEqual(x.pathLossDb, y.pathLossDb));
    EXPECT_TRUE(bitsEqual(x.delaySpreadNs, y.delaySpreadNs));
    EXPECT_TRUE(bitsEqual(x.dopplerHz, y.dopplerHz));
    EXPECT_EQ(x.servingTransmitterId, y.servingTransmitterId);
    EXPECT_TRUE(bitsEqual(x.sinrDb, y.sinrDb));
    EXPECT_TRUE(bitsEqual(x.interferencePowerDbm, y.interferencePowerDbm));
  }
}

// Single-wall image-method scene with several receivers so the per-receiver
// reflection loop has real work. `shift` moves the wall along +y so two scenes
// can share the SAME triangle count but differ in coordinates (content test).
Scene wallScene(double shift = 0.0) {
  Scene scene;
  scene.addMaterial(materials::preset("concrete"));
  const double y = 100.0 + shift;
  const Vec3 a{0, y, 0}, b{300, y, 0}, c{300, y, 50}, d{0, y, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");

  Transmitter tx0;
  tx0.id = "tx0";
  tx0.position = {100, 20, 20};
  tx0.frequencyHz = 3.5e9;
  tx0.powerDbm = 43.0;
  scene.addTransmitter(tx0);
  Transmitter tx1;
  tx1.id = "tx1";
  tx1.position = {220, 30, 25};
  tx1.frequencyHz = 3.5e9;
  tx1.powerDbm = 40.0;
  scene.addTransmitter(tx1);

  for (int i = 0; i < 12; ++i) {
    Receiver rx;
    rx.id = "rx" + std::to_string(i);
    rx.position = {20.0 + 10.0 * i, 20.0 + (i % 3) * 5.0, 10.0};
    scene.addReceiver(rx);
  }
  return scene;
}

CoverageGrid coverageGrid() {
  CoverageGrid g;
  g.origin = {0, 0, 0};
  g.cellSize = 5.0;
  g.cols = 40;
  g.rows = 30;
  g.height = 1.5;
  return g;
}

Route straightRoute() {
  Route r;
  r.id = "drive";
  r.waypoints = {Vec3{0, 0, 1.5}, Vec3{300, 40, 1.5}};
  r.sampleSpacing = 10.0;
  return r;
}

SimulationSettings imageMethodSettings() {
  SimulationSettings s;
  s.mode = PropagationMode::ImageMethod;
  s.maxReflections = 1;
  s.enableSinr = true;
  return s;
}

}  // namespace

// --- Reuse: an unchanged scene builds the backend exactly once ---------------

TEST(Phase3BackendReuse, RepeatedRunBuildsOnce) {
  const Scene scene = wallScene();
  Simulator sim(imageMethodSettings());

  const RFResult first = sim.run(scene);
  EXPECT_EQ(sim.backendRebuildCount(), 1u);
  for (int rep = 0; rep < 5; ++rep) {
    const RFResult r = sim.run(scene);
    EXPECT_EQ(sim.backendRebuildCount(), 1u);
    expectResultEqual(first, r);
  }
}

TEST(Phase3BackendReuse, RepeatedCoverageBuildsOnce) {
  const Scene scene = wallScene();
  const CoverageGrid g = coverageGrid();
  Simulator sim(imageMethodSettings());

  const CoverageResult first = sim.runCoverage(scene, g);
  EXPECT_EQ(sim.backendRebuildCount(), 1u);
  for (int rep = 0; rep < 5; ++rep) {
    const CoverageResult c = sim.runCoverage(scene, g);
    EXPECT_EQ(sim.backendRebuildCount(), 1u);
    expectCoverageEqual(first, c);
  }
}

TEST(Phase3BackendReuse, RepeatedRouteBuildsOnce) {
  const Scene scene = wallScene();
  const Route route = straightRoute();
  Simulator sim(imageMethodSettings());

  const RouteResult first = sim.runRoute(scene, route);
  EXPECT_EQ(sim.backendRebuildCount(), 1u);
  for (int rep = 0; rep < 5; ++rep) {
    const RouteResult r = sim.runRoute(scene, route);
    EXPECT_EQ(sim.backendRebuildCount(), 1u);
    expectRouteEqual(first, r);
  }
}

// Mixed entry points on ONE unchanged scene still build the backend only once.
TEST(Phase3BackendReuse, MixedEntryPointsShareOneBuild) {
  const Scene scene = wallScene();
  Simulator sim(imageMethodSettings());

  sim.run(scene);
  sim.runCoverage(scene, coverageGrid());
  sim.runRoute(scene, straightRoute());
  sim.run(scene);
  EXPECT_EQ(sim.backendRebuildCount(), 1u);
}

// --- Invalidation: any geometry change rebuilds ------------------------------

// A triangle-count change (extra mesh appended) invalidates the cache.
TEST(Phase3BackendReuse, TriangleCountChangeRebuilds) {
  Simulator sim(imageMethodSettings());

  const Scene sceneA = wallScene();
  sim.run(sceneA);
  EXPECT_EQ(sim.backendRebuildCount(), 1u);

  Scene sceneB = wallScene();  // add a second wall => different triangle count
  const Vec3 a{0, 200, 0}, b{300, 200, 0}, c{300, 200, 50}, d{0, 200, 50};
  sceneB.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");
  ASSERT_NE(sceneA.triangles().size(), sceneB.triangles().size());

  sim.run(sceneB);
  EXPECT_EQ(sim.backendRebuildCount(), 2u);
  sim.run(sceneB);  // unchanged again => no further rebuild
  EXPECT_EQ(sim.backendRebuildCount(), 2u);
}

// Same triangle COUNT, different coordinates => the content hash still changes,
// proving the key is not merely count/pointer-based.
TEST(Phase3BackendReuse, EqualCountDifferentCoordsRebuilds) {
  Simulator sim(imageMethodSettings());

  const Scene sceneA = wallScene(0.0);
  const Scene sceneB = wallScene(5.0);  // wall shifted; identical triangle count
  ASSERT_EQ(sceneA.triangles().size(), sceneB.triangles().size());

  sim.run(sceneA);
  EXPECT_EQ(sim.backendRebuildCount(), 1u);
  sim.run(sceneB);
  EXPECT_EQ(sim.backendRebuildCount(), 2u);
  sim.run(sceneA);  // back to A's geometry => yet another rebuild
  EXPECT_EQ(sim.backendRebuildCount(), 3u);
}

// --- Results identical to building the backend per call ----------------------

TEST(Phase3BackendReuse, ReusedRunMatchesFreshBuild) {
  const Scene scene = wallScene();
  const SimulationSettings s = imageMethodSettings();

  const RFResult fresh = Simulator(s).run(scene);  // fresh instance => new build

  Simulator reused(s);
  reused.run(scene);                       // build
  const RFResult cached = reused.run(scene);  // reuse
  EXPECT_EQ(reused.backendRebuildCount(), 1u);
  expectResultEqual(fresh, cached);
}

TEST(Phase3BackendReuse, ReusedCoverageMatchesFreshBuild) {
  const Scene scene = wallScene();
  const CoverageGrid g = coverageGrid();
  const SimulationSettings s = imageMethodSettings();

  const CoverageResult fresh = Simulator(s).runCoverage(scene, g);

  Simulator reused(s);
  reused.runCoverage(scene, g);
  const CoverageResult cached = reused.runCoverage(scene, g);
  EXPECT_EQ(reused.backendRebuildCount(), 1u);
  expectCoverageEqual(fresh, cached);
}

TEST(Phase3BackendReuse, ReusedRouteMatchesFreshBuild) {
  const Scene scene = wallScene();
  const Route route = straightRoute();
  const SimulationSettings s = imageMethodSettings();

  const RouteResult fresh = Simulator(s).runRoute(scene, route);

  Simulator reused(s);
  reused.runRoute(scene, route);
  const RouteResult cached = reused.runRoute(scene, route);
  EXPECT_EQ(reused.backendRebuildCount(), 1u);
  expectRouteEqual(fresh, cached);
}

// After a geometry change, the rebuilt backend yields the SAME result as a fresh
// per-call Simulator on the changed scene (correct new results, not stale).
TEST(Phase3BackendReuse, RebuildAfterChangeMatchesFresh) {
  const SimulationSettings s = imageMethodSettings();
  const Scene sceneA = wallScene(0.0);
  const Scene sceneB = wallScene(7.5);

  Simulator sim(s);
  sim.run(sceneA);
  const RFResult reusedB = sim.run(sceneB);
  EXPECT_EQ(sim.backendRebuildCount(), 2u);

  const RFResult freshB = Simulator(s).run(sceneB);
  expectResultEqual(freshB, reusedB);
}
