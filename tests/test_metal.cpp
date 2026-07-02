// Metal backend parity tests. Built only when RFTRACE_HAVE_METAL is defined
// (RFTRACE_ENABLE_METAL=ON on Apple); every test skips at runtime when no
// Metal device is present, so the default CI is unaffected.
#if RFTRACE_HAVE_METAL

#include <cmath>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "rftrace/backend.hpp"
#include "rftrace/geometry.hpp"
#include "rftrace/math.hpp"

using namespace rftrace;

namespace {

// A wall in the x=5 plane spanning y,z in [0,4], as two triangles. The two
// triangles are split by the diagonal y+z=4, so interior points are
// well-separated for a deterministic triangle-index comparison.
std::vector<Triangle> makeWall() {
  return {
      {Vec3(5, 0, 0), Vec3(5, 4, 0), Vec3(5, 0, 4)},  // tri 0: y+z < 4
      {Vec3(5, 4, 0), Vec3(5, 4, 4), Vec3(5, 0, 4)},  // tri 1: y+z > 4
  };
}

// Parity tolerance for the float32 GPU vs. double CPU (D4).
constexpr double kAbsT = 1e-2;

class MetalParity : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!backendAvailable(Backend::Metal))
      GTEST_SKIP() << "no Metal device available";
    cpu_ = makeBackend(Backend::CPU, false);
    gpu_ = makeBackend(Backend::Metal, false);
    ASSERT_EQ(gpu_->kind(), Backend::Metal);
    const auto wall = makeWall();
    cpu_->build(wall);
    gpu_->build(wall);
  }

  std::unique_ptr<IBackend> cpu_;
  std::unique_ptr<IBackend> gpu_;
};

TEST_F(MetalParity, ClosestHitBatchMatchesCpu) {
  const std::vector<Ray> rays = {
      Ray(Vec3(0, 1, 1), Vec3(1, 0, 0)),             // tri 0 interior
      Ray(Vec3(0, 3, 3), Vec3(1, 0, 0)),             // tri 1 interior
      Ray(Vec3(0, 1, 1), Vec3(-1, 0, 0)),            // miss: away from wall
      Ray(Vec3(0, 10, 10), Vec3(1, 0, 0)),           // miss: above quad
      Ray(Vec3(0, 1, 1), Vec3(1, 0, 0), 1e-4, 3.0),  // miss: tMax before wall
  };

  const auto cpuHits = cpu_->closestHitBatch(rays);
  const auto gpuHits = gpu_->closestHitBatch(rays);
  ASSERT_EQ(cpuHits.size(), rays.size());
  ASSERT_EQ(gpuHits.size(), rays.size());

  for (std::size_t i = 0; i < rays.size(); ++i) {
    EXPECT_EQ(cpuHits[i].valid, gpuHits[i].valid) << "ray " << i;
    if (cpuHits[i].valid && gpuHits[i].valid) {
      EXPECT_EQ(cpuHits[i].triangle, gpuHits[i].triangle) << "ray " << i;
      EXPECT_NEAR(cpuHits[i].t, gpuHits[i].t, kAbsT) << "ray " << i;
    }
  }
}

TEST_F(MetalParity, SingleRayMatchesBatch) {
  const Ray ray(Vec3(0, 2.5, 0.5), Vec3(1, 0, 0));  // tri 0 interior
  const Hit cpuHit = cpu_->closestHit(ray);
  const Hit gpuHit = gpu_->closestHit(ray);
  EXPECT_TRUE(cpuHit.valid);
  EXPECT_TRUE(gpuHit.valid);
  EXPECT_EQ(cpuHit.triangle, gpuHit.triangle);
  EXPECT_NEAR(cpuHit.t, gpuHit.t, kAbsT);
}

TEST_F(MetalParity, OccludedBatchMatchesCpu) {
  const std::vector<Ray> rays = {
      Ray(Vec3(0, 1, 1), Vec3(1, 0, 0)),             // occluded
      Ray(Vec3(0, 3, 3), Vec3(1, 0, 0)),             // occluded
      Ray(Vec3(0, 1, 1), Vec3(-1, 0, 0)),            // clear
      Ray(Vec3(0, 1, 1), Vec3(1, 0, 0), 1e-4, 3.0),  // clear: wall past tMax
  };

  const auto cpuOcc = cpu_->occludedBatch(rays);
  const auto gpuOcc = gpu_->occludedBatch(rays);
  ASSERT_EQ(cpuOcc.size(), rays.size());
  ASSERT_EQ(gpuOcc.size(), rays.size());
  for (std::size_t i = 0; i < rays.size(); ++i)
    EXPECT_EQ(cpuOcc[i] != 0, gpuOcc[i] != 0) << "ray " << i;
}

TEST_F(MetalParity, EmptyBatchReturnsEmpty) {
  EXPECT_TRUE(gpu_->closestHitBatch({}).empty());
  EXPECT_TRUE(gpu_->occludedBatch({}).empty());
}

}  // namespace

#endif  // RFTRACE_HAVE_METAL
