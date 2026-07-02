#include "rftrace/bvh.hpp"

#include <algorithm>
#include <array>
#include <numeric>

namespace rftrace {

void BVH::build(std::vector<Triangle> triangles) {
  triangles_ = std::move(triangles);
  nodes_.clear();
  indices_.resize(triangles_.size());
  std::iota(indices_.begin(), indices_.end(), 0u);

  rootBounds_ = AABB{};
  for (const Triangle& t : triangles_) rootBounds_.expand(t);

  if (triangles_.empty()) return;

  nodes_.reserve(2 * triangles_.size());
  nodes_.push_back(Node{});
  buildRange(0, 0, static_cast<std::uint32_t>(triangles_.size()));
}

void BVH::buildRange(std::uint32_t nodeIndex, std::uint32_t begin,
                     std::uint32_t end) {
  AABB box;
  for (std::uint32_t i = begin; i < end; ++i)
    box.expand(triangles_[indices_[i]]);
  nodes_[nodeIndex].box = box;

  const std::uint32_t count = end - begin;
  if (count <= kLeafSize) {
    nodes_[nodeIndex].left = -1;
    nodes_[nodeIndex].start = begin;
    nodes_[nodeIndex].count = count;
    return;
  }

  // Split along the widest axis of the centroid bounds at the spatial median.
  AABB centroidBox;
  for (std::uint32_t i = begin; i < end; ++i)
    centroidBox.expand(triangles_[indices_[i]].centroid());
  const Vec3 ext = centroidBox.extent();
  int axis = 0;
  if (ext.y() > ext.x()) axis = 1;
  if (ext.z() > ext[axis]) axis = 2;

  const double mid = centroidBox.center()[axis];
  auto middle = std::partition(
      indices_.begin() + begin, indices_.begin() + end,
      [&](std::uint32_t tri) {
        return triangles_[tri].centroid()[axis] < mid;
      });
  std::uint32_t split = static_cast<std::uint32_t>(middle - indices_.begin());

  // Guard against a degenerate split (all centroids coincident on this axis).
  if (split == begin || split == end) split = begin + count / 2;

  const std::int32_t leftIndex = static_cast<std::int32_t>(nodes_.size());
  nodes_[nodeIndex].left = leftIndex;
  nodes_.push_back(Node{});  // left
  nodes_.push_back(Node{});  // right
  buildRange(static_cast<std::uint32_t>(leftIndex), begin, split);
  buildRange(static_cast<std::uint32_t>(leftIndex + 1), split, end);
}

std::vector<BVH::FlatNode> BVH::flatNodes() const {
  std::vector<FlatNode> out;
  out.reserve(nodes_.size());
  for (const Node& n : nodes_)
    out.push_back(FlatNode{n.box.lo, n.box.hi, n.left, n.start, n.count});
  return out;
}

Hit BVH::closestHit(const Ray& ray) const {
  Hit best;
  if (nodes_.empty()) return best;

  std::array<std::int32_t, 64> stack;
  int sp = 0;
  stack[sp++] = 0;

  while (sp > 0) {
    const Node& node = nodes_[stack[--sp]];
    double tNear;
    Ray probe = ray;
    probe.tMax = best.valid ? best.t : ray.tMax;
    if (!node.box.intersect(probe, tNear)) continue;

    if (node.leaf()) {
      for (std::uint32_t i = 0; i < node.count; ++i) {
        const std::uint32_t tri = indices_[node.start + i];
        Ray r = ray;
        r.tMax = best.valid ? best.t : ray.tMax;
        const Hit h = intersectTriangle(r, triangles_[tri],
                                        static_cast<int>(tri));
        if (h.valid && h.t < best.t) best = h;
      }
    } else {
      stack[sp++] = node.left;
      stack[sp++] = node.left + 1;
    }
  }
  return best;
}

bool BVH::occluded(const Ray& ray) const {
  if (nodes_.empty()) return false;

  std::array<std::int32_t, 64> stack;
  int sp = 0;
  stack[sp++] = 0;

  while (sp > 0) {
    const Node& node = nodes_[stack[--sp]];
    double tNear;
    if (!node.box.intersect(ray, tNear)) continue;

    if (node.leaf()) {
      for (std::uint32_t i = 0; i < node.count; ++i) {
        const std::uint32_t tri = indices_[node.start + i];
        if (intersectTriangle(ray, triangles_[tri], static_cast<int>(tri))
                .valid)
          return true;
      }
    } else {
      stack[sp++] = node.left;
      stack[sp++] = node.left + 1;
    }
  }
  return false;
}

}  // namespace rftrace
