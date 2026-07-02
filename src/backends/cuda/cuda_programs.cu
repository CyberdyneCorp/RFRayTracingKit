// OptiX device programs for the CUDA ray-tracing backend: raygen + closesthit +
// miss (closest-hit path) and an any-hit-free occlusion path selected by a
// launch-parameter flag. Compiled to PTX by nvcc (CUDA_PTX_COMPILATION) and
// loaded at runtime via optixModuleCreate. RF physics stays double on the host;
// here everything is float32 (the acceleration structure is built in float).
//
// UNVERIFIED on non-NVIDIA hosts: this file is compiled only when the CUDA
// Toolkit + OptiX SDK are present (RFTRACE_ENABLE_CUDA=ON). It follows the
// standard OptiX 7/8 SDK program structure and the Metal backend's semantics.

#include <optix.h>

#include <cuda_runtime.h>

#include "cuda_shared.h"

using namespace rftrace::cuda;

// Single launch-params symbol referenced by every program group in the module.
extern "C" {
__constant__ LaunchParams params;
}

// Payload register layout (uint32 slots) carried out of optixTrace:
//   p0 = valid (0/1)
//   p1 = t          (float bits)
//   p2 = prim
//   p3 = u          (float bits)
//   p4 = v          (float bits)

extern "C" __global__ void __raygen__rg() {
  const unsigned int idx = optixGetLaunchIndex().x;
  if (idx >= params.count) return;

  const GpuRay r = params.rays[idx];
  const float3 origin = make_float3(r.ox, r.oy, r.oz);
  const float3 dir = make_float3(r.dx, r.dy, r.dz);

  unsigned int valid = 0u;
  unsigned int tbits = 0u;
  unsigned int prim = 0u;
  unsigned int ubits = 0u;
  unsigned int vbits = 0u;

  const unsigned int flags =
      params.occlusion
          ? (OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT | OPTIX_RAY_FLAG_DISABLE_ANYHIT)
          : OPTIX_RAY_FLAG_DISABLE_ANYHIT;

  optixTrace(static_cast<OptixTraversableHandle>(params.handle), origin, dir,
             r.tmin, r.tmax, 0.0f, OptixVisibilityMask(255), flags,
             /*SBToffset=*/0, /*SBTstride=*/0, /*missSBTIndex=*/0, valid, tbits,
             prim, ubits, vbits);

  GpuHit h;
  h.valid = valid;
  h.t = __uint_as_float(tbits);
  h.prim = prim;
  h.u = __uint_as_float(ubits);
  h.v = __uint_as_float(vbits);
  if (valid == 0u) {
    h.t = 0.0f;
    h.prim = 0u;
    h.u = 0.0f;
    h.v = 0.0f;
  }
  params.hits[idx] = h;
}

extern "C" __global__ void __miss__ms() {
  optixSetPayload_0(0u);  // valid = 0
}

extern "C" __global__ void __closesthit__ch() {
  const float2 bary = optixGetTriangleBarycentrics();
  optixSetPayload_0(1u);  // valid
  optixSetPayload_1(__float_as_uint(optixGetRayTmax()));  // hit distance
  optixSetPayload_2(optixGetPrimitiveIndex());
  optixSetPayload_3(__float_as_uint(bary.x));
  optixSetPayload_4(__float_as_uint(bary.y));
}
