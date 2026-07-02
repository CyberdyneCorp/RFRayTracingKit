// Shared POD layouts for the CUDA/OptiX backend, included by BOTH the host
// translation unit (cuda_backend.cpp) and the device programs (cuda_programs.cu)
// so the two sides agree byte-for-byte. Only plain scalars are used (no float3)
// to avoid CUDA alignment surprises across the host/device boundary.
//
// UNVERIFIED on non-NVIDIA hosts: this file is compiled only when the CUDA
// Toolkit + OptiX SDK are present (RFTRACE_ENABLE_CUDA=ON).

#ifndef RFTRACE_CUDA_SHARED_H
#define RFTRACE_CUDA_SHARED_H

#include <cstdint>

namespace rftrace {
namespace cuda {

// One ray, float32, matching the OpenCL/Metal boundary layout.
struct GpuRay {
  float ox, oy, oz;  // origin
  float dx, dy, dz;  // direction
  float tmin;
  float tmax;
};

// One hit record written per ray. `prim` is the OptiX primitive index, which
// equals our Triangle index (identity index buffer / one geometry).
struct GpuHit {
  float t;
  std::uint32_t prim;
  float u;
  float v;
  std::uint32_t valid;
};

// Pin the boundary layouts so host and device agree byte-for-byte (D5). All
// members are 4-byte scalars, so each struct is tightly packed with no float3
// padding — identical to the OpenCL backend's mirrors.
static_assert(sizeof(GpuRay) == 32, "GpuRay layout must match the device");
static_assert(sizeof(GpuHit) == 20, "GpuHit layout must match the device");

// Launch parameters uploaded once per optixLaunch. `occlusion` selects the
// terminate-on-first-hit path in the single raygen program.
struct LaunchParams {
  const GpuRay* rays;
  GpuHit* hits;
  std::uint32_t count;
  std::uint32_t occlusion;  // 0 = closest hit, 1 = occlusion (any hit)
  unsigned long long handle;  // OptixTraversableHandle (opaque 64-bit)
};

}  // namespace cuda
}  // namespace rftrace

#endif  // RFTRACE_CUDA_SHARED_H
