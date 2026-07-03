#pragma once

// Internal spatial index over receiver positions for the batched `rayLaunch`
// capture. Header-only, `detail`-only; not part of the public API.
//
// Problem: the batched capture tests every receiver against every ray segment,
// i.e. O(rays x bounces x receivers) — the dominant CPU cost on a coverage grid.
// This uniform hash grid, built once per `rayLaunch` call, lets a segment query
// only the receivers whose cells its capture tube can reach. `query` returns a
// SUPERSET of the in-range receivers; the caller re-applies the exact
// `distancePointToSegmentSq(...) <= capture2` test, so results are unchanged.
//
// Exactness (superset guarantee): with cell size h = 2*sqrt(capture2), for any
// segment the union of the visited DDA cells' closed boxes contains the whole
// clipped segment, and every receiver within sqrt(capture2) of that segment
// lies in the 3x3x3 halo of some visited cell (R/h = 1/2 => Chebyshev-1). So no
// in-range receiver is ever missed; the exact test then filters to precisely the
// brute-force set. Widening (AABB inflation + halo) only grows the superset.
//
// Non-reentrant: `query` mutates the mutable per-receiver visit stamp for O(1)
// dedup. The `rayLaunch` Phase B capture replay is serial and the threading
// phase does not parallelize this path, so a single grid is never queried from
// two threads. Do not share one instance across threads.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

#include "rftrace/math.hpp"
#include "rftrace/scene.hpp"

namespace rftrace::detail {

class ReceiverGrid {
 public:
  ReceiverGrid(const std::vector<Receiver>& receivers, double capture2)
      : receivers_(receivers) {
    const int nRx = static_cast<int>(receivers.size());
    // effR_ = sqrt(capture2) = |captureRadius|, matching the reference's
    // capture2 = captureRadius*captureRadius exactly (incl. negative radii).
    effR_ = std::sqrt(capture2 > 0.0 ? capture2 : 0.0);
    h_ = 2.0 * effR_;  // cell size = capture diameter (per mandate)
    stamp_.assign(static_cast<std::size_t>(nRx), 0);

    // Degenerate (zero/negative radius) or empty -> full-scan fallback, still a
    // trivial superset.
    if (nRx == 0 || h_ <= 0.0) {
      brute_ = true;
      return;
    }

    gridMin_ = receivers[0].position;
    gridMax_ = receivers[0].position;
    for (const Receiver& rx : receivers) {
      gridMin_ = gridMin_.cwiseMin(rx.position);
      gridMax_ = gridMax_.cwiseMax(rx.position);
    }
    for (int r = 0; r < nRx; ++r)
      cells_[cellIndexOf(receivers[r].position)].push_back(r);
  }

  /// Appends a SUPERSET of {r : distancePointToSegmentSq(pos_r, a, b) <=
  /// capture2} to `cand`. Each index appears at most once. Caller clears `cand`.
  void query(const Vec3& a, const Vec3& b, std::vector<int>& cand) const {
    const int nRx = static_cast<int>(receivers_.size());
    // Bump the dedup epoch so no per-query clearing of `stamp_` is needed.
    if (++epoch_ == 0) {  // wrap: reset stamps so stale 0s cannot false-dedup
      std::fill(stamp_.begin(), stamp_.end(), 0u);
      epoch_ = 1;
    }

    if (brute_) {
      for (int r = 0; r < nRx; ++r) cand.push_back(r);
      return;
    }

    // Clip [a,b] to the inflated receiver AABB. The far part of a very long
    // (kFar) segment provably holds no in-range receiver, so dropping it never
    // misses one — and it keeps the DDA span bounded (load-bearing for perf).
    const Vec3 inflate(h_, h_, h_);
    const Vec3 lo = gridMin_ - inflate;
    const Vec3 hi = gridMax_ + inflate;
    double t0 = 0.0, t1 = 1.0;
    if (!clipSegmentToBox(a, b, lo, hi, t0, t1)) return;  // no overlap
    const Vec3 d = b - a;
    const Vec3 a2 = a + t0 * d;
    const Vec3 b2 = a + t1 * d;

    const CellKey start = cellIndexOf(a2);
    const CellKey end = cellIndexOf(b2);

    // Degenerate (zero-length) clipped segment: gather the single cell's halo.
    if ((b2 - a2).squaredNorm() <= 0.0) {
      gatherHalo(start, cand);
      return;
    }

    // Perf/robustness guard: if the DDA would visit more cells than there are
    // receivers, the full scan is cheaper and is a trivial superset. This also
    // caps the worst case at O(nRx), no worse than the original scan.
    const std::int64_t manhattan = std::llabs(end.i - start.i) +
                                   std::llabs(end.j - start.j) +
                                   std::llabs(end.k - start.k);
    if (manhattan + 1 > static_cast<std::int64_t>(nRx)) {
      for (int r = 0; r < nRx; ++r) cand.push_back(r);
      return;
    }

    forEachDdaCell(a2, b2, start, end, cand);
  }

 private:
  /// Integer cell coordinate; a struct key (with equality) so the hash never
  /// drops a bucket even for huge extents / tiny radii (no bit-pack overflow).
  struct CellKey {
    std::int64_t i, j, k;
    bool operator==(const CellKey& o) const {
      return i == o.i && j == o.j && k == o.k;
    }
  };
  struct CellKeyHash {
    std::size_t operator()(const CellKey& c) const noexcept {
      std::uint64_t h = 1469598103934665603ull;  // FNV-1a
      auto mix = [&h](std::int64_t v) {
        h ^= static_cast<std::uint64_t>(v);
        h *= 1099511628211ull;
      };
      mix(c.i);
      mix(c.j);
      mix(c.k);
      return static_cast<std::size_t>(h);
    }
  };

  CellKey cellIndexOf(const Vec3& p) const {
    return CellKey{
        static_cast<std::int64_t>(std::floor((p.x() - gridMin_.x()) / h_)),
        static_cast<std::int64_t>(std::floor((p.y() - gridMin_.y()) / h_)),
        static_cast<std::int64_t>(std::floor((p.z() - gridMin_.z()) / h_))};
  }

  /// Liang-Barsky slab clip of segment [a,b] against box [lo,hi]. Returns the
  /// overlap sub-interval [t0,t1] (t in [0,1]); false if the segment misses.
  static bool clipSegmentToBox(const Vec3& a, const Vec3& b, const Vec3& lo,
                               const Vec3& hi, double& t0, double& t1) {
    t0 = 0.0;
    t1 = 1.0;
    const Vec3 d = b - a;
    for (int ax = 0; ax < 3; ++ax) {
      if (std::abs(d[ax]) < 1e-300) {
        if (a[ax] < lo[ax] || a[ax] > hi[ax]) return false;  // parallel, outside
        continue;
      }
      const double inv = 1.0 / d[ax];
      double tA = (lo[ax] - a[ax]) * inv;
      double tB = (hi[ax] - a[ax]) * inv;
      if (tA > tB) std::swap(tA, tB);
      if (tA > t0) t0 = tA;
      if (tB < t1) t1 = tB;
      if (t0 > t1) return false;
    }
    return true;
  }

  /// Append every not-yet-stamped receiver in the 3x3x3 neighborhood of `v`.
  void gatherHalo(const CellKey& v, std::vector<int>& cand) const {
    for (std::int64_t dx = -1; dx <= 1; ++dx)
      for (std::int64_t dy = -1; dy <= 1; ++dy)
        for (std::int64_t dz = -1; dz <= 1; ++dz) {
          auto it = cells_.find(CellKey{v.i + dx, v.j + dy, v.k + dz});
          if (it == cells_.end()) continue;
          for (int r : it->second) {
            if (stamp_[static_cast<std::size_t>(r)] == epoch_) continue;
            stamp_[static_cast<std::size_t>(r)] = epoch_;
            cand.push_back(r);
          }
        }
  }

  /// Amanatides-Woo 3D DDA: visit every cell the segment [a2,b2] crosses (its
  /// visited closed cells cover the whole segment) and gather each cell's halo.
  void forEachDdaCell(const Vec3& a2, const Vec3& b2, const CellKey& start,
                      const CellKey& end, std::vector<int>& cand) const {
    const Vec3 d = b2 - a2;
    CellKey cur = start;
    std::int64_t step[3];
    double tMax[3], tDelta[3];
    for (int ax = 0; ax < 3; ++ax) {
      const std::int64_t ci = (ax == 0 ? cur.i : ax == 1 ? cur.j : cur.k);
      if (d[ax] > 0.0) {
        step[ax] = 1;
        const double boundary = gridMin_[ax] + (ci + 1) * h_;
        tMax[ax] = (boundary - a2[ax]) / d[ax];
        tDelta[ax] = h_ / d[ax];
      } else if (d[ax] < 0.0) {
        step[ax] = -1;
        const double boundary = gridMin_[ax] + ci * h_;
        tMax[ax] = (boundary - a2[ax]) / d[ax];
        tDelta[ax] = h_ / (-d[ax]);
      } else {
        step[ax] = 0;
        tMax[ax] = std::numeric_limits<double>::infinity();
        tDelta[ax] = std::numeric_limits<double>::infinity();
      }
    }

    // Iteration cap = exact AW step count (manhattan) + margin for FP ties; if
    // ever exceeded, fall back to a full gather (safe superset, never a miss).
    const std::int64_t cap = std::llabs(end.i - start.i) +
                             std::llabs(end.j - start.j) +
                             std::llabs(end.k - start.k) + 4;
    gatherHalo(cur, cand);
    for (std::int64_t iter = 0; iter < cap; ++iter) {
      // Pick the axis whose next boundary crossing has the smallest t.
      const int ax = (tMax[0] <= tMax[1] && tMax[0] <= tMax[2]) ? 0
                     : (tMax[1] <= tMax[2])                     ? 1
                                                                : 2;
      if (tMax[ax] > 1.0) return;  // no boundary crossing remains within [0,1]
      (ax == 0 ? cur.i : ax == 1 ? cur.j : cur.k) += step[ax];
      tMax[ax] += tDelta[ax];
      gatherHalo(cur, cand);
      if (cur == end) return;
    }
    // Cap hit (FP drift on a pathological segment): guarantee no miss.
    for (int r = 0; r < static_cast<int>(receivers_.size()); ++r) {
      if (stamp_[static_cast<std::size_t>(r)] == epoch_) continue;
      stamp_[static_cast<std::size_t>(r)] = epoch_;
      cand.push_back(r);
    }
  }

  const std::vector<Receiver>& receivers_;
  double h_ = 0.0, effR_ = 0.0;
  Vec3 gridMin_{0, 0, 0}, gridMax_{0, 0, 0};
  bool brute_ = false;
  std::unordered_map<CellKey, std::vector<int>, CellKeyHash> cells_;
  mutable std::vector<std::uint32_t> stamp_;
  mutable std::uint32_t epoch_ = 0;
};

}  // namespace rftrace::detail
