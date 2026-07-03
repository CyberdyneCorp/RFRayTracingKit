#pragma once

// Internal groundwork helper for the batched simulator path. Not part of the
// public API.
//
// BatchQuery is a thin gather -> dispatch -> indexed-scatter buffer around the
// backend's caller-owned batched queries (occludedBatchInto /
// closestHitBatchInto). A hot loop gathers rays with add() (each returns a
// stable token = insertion index), issues ONE batched dispatch, then reads the
// per-ray result back by token. The internal buffers are members, so successive
// clear()/add()/run cycles reuse a single allocation.
//
// Batching/reordering occlusion queries cannot change any individual result:
// IBackend::occluded()/closestHit() (and their batched forms) are pure, const,
// side-effect-free ray queries and the CPU default simply loops the single-ray
// query. Callers remain responsible for preserving path-insertion and RNG draw
// order at the scatter site.

#include <cstddef>
#include <span>
#include <vector>

#include "rftrace/backend.hpp"

namespace rftrace::detail {

class BatchQuery {
 public:
  /// Token for a pair that never enters the batch (e.g. a degenerate segment).
  /// Callers treat it as "blocked" / "no hit" at the scatter site.
  static constexpr std::size_t kNoRay = static_cast<std::size_t>(-1);

  /// Reset for a new gather. Keeps buffer capacity for reuse.
  void clear() { rays_.clear(); }

  /// Append a ray to the current gather. Returns its token (insertion index).
  std::size_t add(const Ray& r) {
    rays_.push_back(r);
    return rays_.size() - 1;
  }

  std::size_t size() const { return rays_.size(); }
  bool empty() const { return rays_.empty(); }

  /// Dispatch all gathered rays as one occlusion batch. Skipped (no dispatch)
  /// when empty; resizing to zero is a no-op.
  void runOcclusion(const IBackend& backend) {
    occ_.resize(rays_.size());
    if (!rays_.empty())
      backend.occludedBatchInto(rays_,
                                std::span<char>(occ_.data(), occ_.size()));
  }

  /// Occlusion result for a gathered ray by token.
  bool occluded(std::size_t token) const { return occ_[token] != 0; }

  /// Dispatch all gathered rays as one closest-hit batch (Phases 2-3).
  void runClosestHit(const IBackend& backend) {
    hits_.resize(rays_.size());
    if (!rays_.empty())
      backend.closestHitBatchInto(rays_,
                                  std::span<Hit>(hits_.data(), hits_.size()));
  }

  /// Closest-hit result for a gathered ray by token (Phases 2-3).
  const Hit& hit(std::size_t token) const { return hits_[token]; }

  const std::vector<Ray>& rays() const { return rays_; }

 private:
  std::vector<Ray> rays_;
  std::vector<char> occ_;
  std::vector<Hit> hits_;
};

}  // namespace rftrace::detail
