// Tests for the caller-owned-output batched query API (closestHitBatchInto /
// occludedBatchInto). These run on the always-available CPU backend and assert
// the Into forms agree with the vector-returning forms, and — crucially — that a
// reused/dirty output buffer still yields correct results (every slot written,
// including misses). The CUDA backend's Into overrides are covered under the
// same parity rule in test_cuda_parity.cpp.

#include <array>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "rftrace/backend.hpp"
#include "rftrace/geometry.hpp"
#include "rftrace/math.hpp"

using namespace rftrace;

namespace {

// A wall in the x=5 plane spanning y,z in [0,4], split by the diagonal y+z=4.
std::vector<Triangle> makeWall() {
  return {
      {Vec3(5, 0, 0), Vec3(5, 4, 0), Vec3(5, 0, 4)},
      {Vec3(5, 4, 0), Vec3(5, 4, 4), Vec3(5, 0, 4)},
  };
}

// A mix of rays that hit the wall and rays that miss it, so the buffer holds
// both hit and miss slots.
std::vector<Ray> makeRays() {
  return {
      Ray(Vec3(0, 1, 1), Vec3(1, 0, 0)),    // hits tri 0
      Ray(Vec3(0, 3, 3), Vec3(1, 0, 0)),    // hits tri 1
      Ray(Vec3(0, 1, 1), Vec3(-1, 0, 0)),   // misses (away from wall)
      Ray(Vec3(0, -9, 0), Vec3(0, -1, 0)),  // misses (parallel, off wall)
  };
}

}  // namespace

TEST(BackendCallerOwnedOutput, ClosestHitIntoMatchesVector) {
  auto cpu = makeBackend(Backend::CPU, false);
  cpu->build(makeWall());
  const std::vector<Ray> rays = makeRays();

  const std::vector<Hit> expected = cpu->closestHitBatch(rays);
  std::vector<Hit> out(rays.size());
  cpu->closestHitBatchInto(rays, out);

  ASSERT_EQ(out.size(), expected.size());
  for (std::size_t i = 0; i < rays.size(); ++i) {
    EXPECT_EQ(out[i].valid, expected[i].valid) << "ray " << i;
    EXPECT_EQ(out[i].triangle, expected[i].triangle) << "ray " << i;
    EXPECT_DOUBLE_EQ(out[i].t, expected[i].t) << "ray " << i;
  }
}

TEST(BackendCallerOwnedOutput, OccludedIntoMatchesVector) {
  auto cpu = makeBackend(Backend::CPU, false);
  cpu->build(makeWall());
  const std::vector<Ray> rays = makeRays();

  const std::vector<char> expected = cpu->occludedBatch(rays);
  std::vector<char> out(rays.size());
  cpu->occludedBatchInto(rays, out);
  EXPECT_EQ(out, expected);
}

// A reused buffer carries stale results from a prior batch. The Into contract
// must overwrite every slot, so a miss on the second batch must not inherit a
// hit from the first.
TEST(BackendCallerOwnedOutput, ReusedDirtyBufferIsFullyOverwritten) {
  auto cpu = makeBackend(Backend::CPU, false);
  cpu->build(makeWall());

  // First batch: all hits, filling the buffer with valid results.
  const std::vector<Ray> hitRays = {Ray(Vec3(0, 1, 1), Vec3(1, 0, 0)),
                                    Ray(Vec3(0, 3, 3), Vec3(1, 0, 0))};
  std::vector<Hit> buf(2);
  cpu->closestHitBatchInto(hitRays, buf);
  ASSERT_TRUE(buf[0].valid);
  ASSERT_TRUE(buf[1].valid);

  // Second batch into the same buffer: both miss. Stale valid=true must clear.
  const std::vector<Ray> missRays = {Ray(Vec3(0, 1, 1), Vec3(-1, 0, 0)),
                                     Ray(Vec3(0, 3, 3), Vec3(-1, 0, 0))};
  cpu->closestHitBatchInto(missRays, buf);
  EXPECT_FALSE(buf[0].valid);
  EXPECT_FALSE(buf[1].valid);

  std::vector<char> occ(2, 1);  // pre-dirtied to 1
  cpu->occludedBatchInto(missRays, occ);
  EXPECT_EQ(occ[0], 0);
  EXPECT_EQ(occ[1], 0);
}

// The vector-returning forms are thin wrappers over the Into primitive; a
// std::array span (non-vector storage) is a valid caller buffer too.
TEST(BackendCallerOwnedOutput, AcceptsNonVectorSpanStorage) {
  auto cpu = makeBackend(Backend::CPU, false);
  cpu->build(makeWall());
  const std::vector<Ray> rays = {Ray(Vec3(0, 1, 1), Vec3(1, 0, 0))};

  std::array<Hit, 1> storage{};
  cpu->closestHitBatchInto(rays, std::span<Hit>(storage));
  EXPECT_TRUE(storage[0].valid);
  EXPECT_EQ(storage[0].triangle, 0);
}
