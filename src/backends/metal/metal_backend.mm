// Metal GPU ray-tracing backend (Objective-C++). Compiled only when
// RFTRACE_ENABLE_METAL is on and the platform is Apple. Builds a hardware
// primitive acceleration structure from the scene triangles and intersects
// batches of rays with a runtime-compiled compute kernel that uses
// metal_raytracing. RF physics stays double-precision on the host; only the
// acceleration structure and ray/hit buffers are float32.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "rftrace/backend.hpp"
#include "rftrace/backends/metal_backend.hpp"

namespace rftrace {
namespace {

// Host-side mirrors of the kernel structs. Layouts must match the .metal source
// below exactly. packed_float3 is 12 bytes (no 16-byte float3 padding), so we
// use three explicit floats to guarantee the same layout on the host.
struct GpuRay {
  float ox, oy, oz;  // origin
  float dx, dy, dz;  // direction
  float tmin;
  float tmax;
};

struct GpuHit {
  float t;
  std::uint32_t prim;
  float u;
  float v;
  std::uint32_t valid;
};

// Runtime-compiled Metal kernel source. Two kernels share the ray/hit buffers;
// the occlusion kernel accepts any intersection and only reports hit/miss.
const char* kKernelSource = R"METAL(
#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace raytracing;

struct GpuRay {
  packed_float3 origin;
  packed_float3 dir;
  float tmin;
  float tmax;
};

struct GpuHit {
  float t;
  uint prim;
  float u;
  float v;
  uint valid;
};

kernel void closestHitKernel(
    device const GpuRay* rays              [[buffer(0)]],
    device GpuHit* hits                    [[buffer(1)]],
    constant uint& count                   [[buffer(2)]],
    primitive_acceleration_structure accel [[buffer(3)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= count) return;
  GpuRay gr = rays[gid];

  ray r;
  r.origin = float3(gr.origin);
  r.direction = float3(gr.dir);
  r.min_distance = gr.tmin;
  r.max_distance = gr.tmax;

  intersector<triangle_data> isect;
  isect.assume_geometry_type(geometry_type::triangle);
  intersection_result<triangle_data> res = isect.intersect(r, accel);

  GpuHit h;
  if (res.type == intersection_type::triangle) {
    h.valid = 1u;
    h.t = res.distance;
    h.prim = res.primitive_id;
    float2 bary = res.triangle_barycentric_coord;
    h.u = bary.x;
    h.v = bary.y;
  } else {
    h.valid = 0u;
    h.t = 0.0f;
    h.prim = 0u;
    h.u = 0.0f;
    h.v = 0.0f;
  }
  hits[gid] = h;
}

kernel void occludedKernel(
    device const GpuRay* rays              [[buffer(0)]],
    device GpuHit* hits                    [[buffer(1)]],
    constant uint& count                   [[buffer(2)]],
    primitive_acceleration_structure accel [[buffer(3)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= count) return;
  GpuRay gr = rays[gid];

  ray r;
  r.origin = float3(gr.origin);
  r.direction = float3(gr.dir);
  r.min_distance = gr.tmin;
  r.max_distance = gr.tmax;

  intersector<triangle_data> isect;
  isect.assume_geometry_type(geometry_type::triangle);
  isect.accept_any_intersection(true);
  intersection_result<triangle_data> res = isect.intersect(r, accel);

  GpuHit h = {};
  h.valid = (res.type != intersection_type::none) ? 1u : 0u;
  hits[gid] = h;
}
)METAL";

[[noreturn]] void fail(const std::string& what) {
  throw std::runtime_error("Metal backend: " + what);
}

GpuRay toGpuRay(const Ray& ray) {
  // Clamp an infinite tMax to the largest finite float the kernel can use.
  const double tmax = std::isinf(ray.tMax) ? 3.0e38 : ray.tMax;
  return GpuRay{
      static_cast<float>(ray.origin.x()), static_cast<float>(ray.origin.y()),
      static_cast<float>(ray.origin.z()), static_cast<float>(ray.direction.x()),
      static_cast<float>(ray.direction.y()),
      static_cast<float>(ray.direction.z()), static_cast<float>(ray.tMin),
      static_cast<float>(tmax)};
}

class MetalBackend final : public IBackend {
 public:
  MetalBackend() {
    device_ = MTLCreateSystemDefaultDevice();
    if (device_ == nil || !device_.supportsRaytracing)
      fail("no Metal device with ray-tracing support");

    queue_ = [device_ newCommandQueue];
    if (queue_ == nil) fail("failed to create command queue");

    NSError* err = nil;
    NSString* src = [NSString stringWithUTF8String:kKernelSource];
    id<MTLLibrary> lib = [device_ newLibraryWithSource:src
                                               options:nil
                                                 error:&err];
    if (lib == nil)
      fail(std::string("kernel compile failed: ") +
           (err ? err.localizedDescription.UTF8String : "unknown"));

    closestPipeline_ = makePipeline(lib, @"closestHitKernel");
    occludedPipeline_ = makePipeline(lib, @"occludedKernel");
  }

  void build(const std::vector<Triangle>& triangles) override {
    triangleCount_ = triangles.size();
    accel_ = nil;
    vertexBuffer_ = nil;
    indexBuffer_ = nil;
    if (triangleCount_ == 0) return;

    // Pack vertices as float3 triplets (one per triangle) with an identity
    // index buffer, so primitive index == our triangle index.
    std::vector<float> verts;
    verts.reserve(triangleCount_ * 9);
    std::vector<std::uint32_t> indices;
    indices.reserve(triangleCount_ * 3);
    std::uint32_t idx = 0;
    for (const Triangle& t : triangles) {
      for (const Vec3* v : {&t.v0, &t.v1, &t.v2}) {
        verts.push_back(static_cast<float>(v->x()));
        verts.push_back(static_cast<float>(v->y()));
        verts.push_back(static_cast<float>(v->z()));
        indices.push_back(idx++);
      }
    }

    vertexBuffer_ = [device_ newBufferWithBytes:verts.data()
                                         length:verts.size() * sizeof(float)
                                        options:MTLResourceStorageModeShared];
    indexBuffer_ =
        [device_ newBufferWithBytes:indices.data()
                             length:indices.size() * sizeof(std::uint32_t)
                            options:MTLResourceStorageModeShared];

    MTLAccelerationStructureTriangleGeometryDescriptor* geom =
        [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
    geom.vertexBuffer = vertexBuffer_;
    geom.vertexBufferOffset = 0;
    geom.vertexStride = 3 * sizeof(float);
    geom.triangleCount = triangleCount_;
    geom.indexBuffer = indexBuffer_;
    geom.indexBufferOffset = 0;
    geom.indexType = MTLIndexTypeUInt32;

    MTLPrimitiveAccelerationStructureDescriptor* desc =
        [MTLPrimitiveAccelerationStructureDescriptor descriptor];
    desc.geometryDescriptors = @[ geom ];

    MTLAccelerationStructureSizes sizes =
        [device_ accelerationStructureSizesWithDescriptor:desc];
    accel_ = [device_ newAccelerationStructureWithSize:sizes.accelerationStructureSize];
    id<MTLBuffer> scratch =
        [device_ newBufferWithLength:sizes.buildScratchBufferSize
                             options:MTLResourceStorageModePrivate];
    if (accel_ == nil) fail("failed to allocate acceleration structure");

    id<MTLCommandBuffer> cmd = [queue_ commandBuffer];
    id<MTLAccelerationStructureCommandEncoder> enc =
        [cmd accelerationStructureCommandEncoder];
    [enc buildAccelerationStructure:accel_
                         descriptor:desc
                      scratchBuffer:scratch
                scratchBufferOffset:0];
    [enc endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
    if (cmd.status != MTLCommandBufferStatusCompleted)
      fail("acceleration structure build did not complete");
  }

  Hit closestHit(const Ray& ray) const override {
    return closestHitBatch({ray}).front();
  }

  bool occluded(const Ray& ray) const override {
    return occludedBatch({ray}).front() != 0;
  }

  std::vector<Hit> closestHitBatch(
      const std::vector<Ray>& rays) const override {
    std::vector<Hit> out(rays.size());
    if (rays.empty()) return out;
    std::vector<GpuHit> gpuHits = dispatch(rays, closestPipeline_);
    for (std::size_t i = 0; i < rays.size(); ++i) {
      const GpuHit& g = gpuHits[i];
      if (g.valid) {
        out[i].valid = true;
        out[i].t = g.t;
        out[i].u = g.u;
        out[i].v = g.v;
        out[i].triangle = static_cast<int>(g.prim);
      }
    }
    return out;
  }

  std::vector<char> occludedBatch(const std::vector<Ray>& rays) const override {
    std::vector<char> out(rays.size(), 0);
    if (rays.empty()) return out;
    std::vector<GpuHit> gpuHits = dispatch(rays, occludedPipeline_);
    for (std::size_t i = 0; i < rays.size(); ++i)
      out[i] = gpuHits[i].valid ? 1 : 0;
    return out;
  }

  Backend kind() const override { return Backend::Metal; }

 private:
  id<MTLComputePipelineState> makePipeline(id<MTLLibrary> lib, NSString* name) {
    id<MTLFunction> fn = [lib newFunctionWithName:name];
    if (fn == nil) fail(std::string("missing kernel ") + name.UTF8String);
    NSError* err = nil;
    id<MTLComputePipelineState> pso =
        [device_ newComputePipelineStateWithFunction:fn error:&err];
    if (pso == nil)
      fail(std::string("pipeline creation failed: ") +
           (err ? err.localizedDescription.UTF8String : "unknown"));
    return pso;
  }

  std::vector<GpuHit> dispatch(const std::vector<Ray>& rays,
                               id<MTLComputePipelineState> pso) const {
    const std::uint32_t count = static_cast<std::uint32_t>(rays.size());
    std::vector<GpuHit> result(count);
    if (accel_ == nil || triangleCount_ == 0) return result;  // empty scene: all miss

    std::vector<GpuRay> gpuRays(count);
    for (std::uint32_t i = 0; i < count; ++i) gpuRays[i] = toGpuRay(rays[i]);

    id<MTLBuffer> rayBuf =
        [device_ newBufferWithBytes:gpuRays.data()
                             length:gpuRays.size() * sizeof(GpuRay)
                            options:MTLResourceStorageModeShared];
    id<MTLBuffer> hitBuf =
        [device_ newBufferWithLength:count * sizeof(GpuHit)
                             options:MTLResourceStorageModeShared];

    id<MTLCommandBuffer> cmd = [queue_ commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:pso];
    [enc setBuffer:rayBuf offset:0 atIndex:0];
    [enc setBuffer:hitBuf offset:0 atIndex:1];
    [enc setBytes:&count length:sizeof(count) atIndex:2];
    [enc setAccelerationStructure:accel_ atBufferIndex:3];

    NSUInteger tg = pso.maxTotalThreadsPerThreadgroup;
    if (tg > count) tg = count;
    [enc dispatchThreads:MTLSizeMake(count, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    [enc endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
    if (cmd.status != MTLCommandBufferStatusCompleted)
      fail("compute dispatch did not complete");

    std::memcpy(result.data(), hitBuf.contents, count * sizeof(GpuHit));
    return result;
  }

  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> queue_ = nil;
  id<MTLComputePipelineState> closestPipeline_ = nil;
  id<MTLComputePipelineState> occludedPipeline_ = nil;
  id<MTLAccelerationStructure> accel_ = nil;
  id<MTLBuffer> vertexBuffer_ = nil;
  id<MTLBuffer> indexBuffer_ = nil;
  std::size_t triangleCount_ = 0;
};

}  // namespace

bool metalDeviceAvailable() {
  @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    return dev != nil && dev.supportsRaytracing;
  }
}

std::unique_ptr<IBackend> makeMetalBackend() {
  return std::make_unique<MetalBackend>();
}

}  // namespace rftrace
