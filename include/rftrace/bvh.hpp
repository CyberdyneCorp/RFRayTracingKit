#pragma once

#include <cstdint>
#include <vector>

#include "rftrace/geometry.hpp"

namespace rftrace {

/// A NanoRT-style bounding volume hierarchy over a triangle set. Supports
/// closest-hit and occlusion (any-hit) queries and is the CPU backend's
/// acceleration structure and the correctness reference for other backends.
class BVH {
 public:
  BVH() = default;

  /// Build the hierarchy over a copy of `triangles`. Safe to call with an empty
  /// set (all subsequent queries then miss).
  void build(std::vector<Triangle> triangles);

  /// Nearest triangle intersection along the ray, or an invalid hit.
  Hit closestHit(const Ray& ray) const;

  /// True if any triangle intersects the ray within its interval.
  bool occluded(const Ray& ray) const;

  std::size_t triangleCount() const { return triangles_.size(); }
  const AABB& bounds() const { return rootBounds_; }

  /// A BVH node in a flat, GPU-friendly form. `left` is the index of this node's
  /// first child (its sibling is at `left + 1`) or -1 for a leaf; for a leaf,
  /// [start, start + count) indexes into triangleIndices().
  struct FlatNode {
    Vec3 boxMin;
    Vec3 boxMax;
    std::int32_t left = -1;
    std::uint32_t start = 0;
    std::uint32_t count = 0;
  };

  /// Flat node array for GPU traversal (root at index 0; empty for an empty
  /// mesh). Additive accessor — it does not affect CPU traversal or build order.
  std::vector<FlatNode> flatNodes() const;

  /// The triangle-index permutation that leaf nodes reference via [start, count).
  /// Each entry is an index into triangles() / the built triangle set.
  const std::vector<std::uint32_t>& triangleIndices() const { return indices_; }

  /// The triangles in build order; a hit's primitive index indexes this vector.
  const std::vector<Triangle>& triangles() const { return triangles_; }

 private:
  struct Node {
    AABB box;
    std::int32_t left = -1;    ///< child index, or -1 for a leaf
    std::uint32_t start = 0;   ///< first triangle (leaf only)
    std::uint32_t count = 0;   ///< triangle count (leaf only)
    bool leaf() const { return left < 0; }
  };

  void buildRange(std::uint32_t nodeIndex, std::uint32_t begin,
                  std::uint32_t end);

  std::vector<Triangle> triangles_;
  std::vector<std::uint32_t> indices_;  ///< permutation into triangles_
  std::vector<Node> nodes_;
  AABB rootBounds_;
  static constexpr std::uint32_t kLeafSize = 4;
};

}  // namespace rftrace
