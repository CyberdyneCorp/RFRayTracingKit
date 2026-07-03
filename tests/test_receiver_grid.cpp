#include <gtest/gtest.h>

#include <random>
#include <unordered_set>
#include <vector>

#include "rftrace/detail/propagation.hpp"
#include "rftrace/detail/receiver_grid.hpp"
#include "rftrace/scene.hpp"

// Focused, physics-free gate on the ReceiverGrid superset invariant. The
// batched rayLaunch capture depends on ONE property: for any segment [a,b] and
// capture2, query(a,b) returns a SUPERSET of the exact in-range receiver set
// { r : distancePointToSegmentSq(pos_r, a, b) <= capture2 }. If that holds, the
// exact test in captureSegment filters it to precisely the brute-force set and
// results are unchanged. This test asserts the invariant directly over many
// randomized clouds/segments/radii, independent of the physics path, so a
// missed capture surfaces here even if the differential oracle happens to miss
// the regime. It also asserts query yields each index at most once.
using namespace rftrace;

namespace {

Receiver makeRx(const Vec3& p) {
  Receiver r;
  r.position = p;
  return r;
}

// Assert: exact in-range set subset of query(a,b), and query has no duplicates.
void expectSupersetNoDup(const std::vector<Receiver>& rxs, const Vec3& a,
                         const Vec3& b, double captureRadius,
                         const std::string& where) {
  const double capture2 = captureRadius * captureRadius;
  detail::ReceiverGrid grid(rxs, capture2);
  std::vector<int> cand;
  grid.query(a, b, cand);

  // No duplicate indices.
  std::unordered_set<int> candSet(cand.begin(), cand.end());
  ASSERT_EQ(candSet.size(), cand.size()) << where << " query returned duplicates";

  // Every exact in-range receiver must be present in the candidate superset.
  for (int r = 0; r < static_cast<int>(rxs.size()); ++r) {
    const double d2 =
        detail::distancePointToSegmentSq(rxs[r].position, a, b);
    if (d2 <= capture2)
      EXPECT_TRUE(candSet.count(r) == 1)
          << where << " MISSED receiver " << r << " d2=" << d2
          << " capture2=" << capture2;
  }
}

}  // namespace

// Randomized clouds x segments x radii: the superset invariant must always hold.
TEST(ReceiverGrid, SupersetInvariantRandomized) {
  std::mt19937_64 rng(12345);
  std::uniform_real_distribution<double> pos(-200.0, 200.0);
  std::uniform_real_distribution<double> radii(0.0, 60.0);
  std::uniform_int_distribution<int> countDist(0, 400);

  for (int trial = 0; trial < 300; ++trial) {
    const int n = countDist(rng);
    std::vector<Receiver> rxs;
    rxs.reserve(n);
    for (int i = 0; i < n; ++i)
      rxs.push_back(makeRx({pos(rng), pos(rng), pos(rng)}));

    const Vec3 a{pos(rng), pos(rng), pos(rng)};
    const Vec3 b{pos(rng), pos(rng), pos(rng)};
    const double radius = radii(rng);
    expectSupersetNoDup(rxs, a, b, radius,
                        "trial=" + std::to_string(trial));
  }
}

// Duplicate / identical receiver positions share a cell; each must still be
// returned (once) when in range.
TEST(ReceiverGrid, DuplicatePositions) {
  std::vector<Receiver> rxs = {
      makeRx({10, 10, 10}), makeRx({10, 10, 10}), makeRx({10, 10, 10}),
      makeRx({10.0001, 10, 10}), makeRx({-50, -50, -50})};
  for (double radius : {0.0, 0.5, 2.0, 100.0}) {
    expectSupersetNoDup(rxs, {0, 10, 10}, {30, 10, 10}, radius,
                        "dup r=" + std::to_string(radius));
    expectSupersetNoDup(rxs, {10, 10, 10}, {10, 10, 10}, radius,
                        "dup-zeroseg r=" + std::to_string(radius));
  }
}

// Wide extent + tiny radius (many cells) and a very long segment: clip must keep
// the superset while bounding the DDA span.
TEST(ReceiverGrid, WideExtentTinyRadiusAndLongSegment) {
  std::mt19937_64 rng(777);
  std::uniform_real_distribution<double> pos(-5000.0, 5000.0);
  std::vector<Receiver> rxs;
  for (int i = 0; i < 250; ++i)
    rxs.push_back(makeRx({pos(rng), pos(rng), pos(rng)}));

  // Long segment far past the receiver cloud (mirrors segEnd = p + dir*kFar).
  const Vec3 a{-9000, -9000, -9000};
  const Vec3 b{1e6, 1e6, 1e6};
  for (double radius : {0.001, 0.5, 10.0, 500.0, 1e4}) {
    expectSupersetNoDup(rxs, a, b, radius, "long r=" + std::to_string(radius));
    expectSupersetNoDup(rxs, {0, 0, 0}, {1e6, 0, 0}, radius,
                        "axis-long r=" + std::to_string(radius));
  }
}

// Receivers exactly on a cell boundary must not fall through a crack.
TEST(ReceiverGrid, CellBoundaryReceivers) {
  // capture2 = 1 -> effR=1, h=2. Place receivers on multiples of h.
  std::vector<Receiver> rxs;
  for (int i = -5; i <= 5; ++i)
    rxs.push_back(makeRx({2.0 * i, 0, 0}));
  for (double radius : {0.5, 1.0, 1.0000001, 3.0}) {
    expectSupersetNoDup(rxs, {-12, 0, 0}, {12, 0, 0}, radius,
                        "boundary r=" + std::to_string(radius));
    expectSupersetNoDup(rxs, {-12, 0.9, 0}, {12, 0.9, 0}, radius,
                        "boundary-off r=" + std::to_string(radius));
  }
}

// Degenerate inputs: zero receivers, single receiver, zero/negative radius.
TEST(ReceiverGrid, DegenerateCases) {
  expectSupersetNoDup({}, {0, 0, 0}, {10, 0, 0}, 5.0, "empty");
  expectSupersetNoDup({makeRx({5, 0, 0})}, {0, 0, 0}, {10, 0, 0}, 5.0, "single");
  // Zero/negative radius -> brute fallback; still a valid superset.
  std::vector<Receiver> rxs = {makeRx({5, 0, 0}), makeRx({5, 0, 0})};
  expectSupersetNoDup(rxs, {5, 0, 0}, {5, 0, 0}, 0.0, "zero-radius");
  expectSupersetNoDup(rxs, {0, 0, 0}, {10, 0, 0}, -3.0, "negative-radius");
}
