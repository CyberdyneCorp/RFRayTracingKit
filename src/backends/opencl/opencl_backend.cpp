// OpenCL GPU ray-traversal backend. Compiled only when RFTRACE_ENABLE_OPENCL is
// on and find_package(OpenCL) succeeds (RFTRACE_HAVE_OPENCL). OpenCL 1.2 has no
// hardware ray tracing, so this backend builds a custom flat BVH (from the CPU
// BVH's additive flat accessor), uploads it to the device as float32 buffers,
// and intersects batches of rays with a runtime-compiled OpenCL C kernel that
// does iterative (explicit-stack, no recursion) BVH traversal. RF physics stays
// double-precision on the host; only the buffers crossing to the device are
// float32.

#define CL_SILENCE_DEPRECATION
#define CL_TARGET_OPENCL_VERSION 120

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "rftrace/backend.hpp"
#include "rftrace/backends/opencl_backend.hpp"
#include "rftrace/bvh.hpp"

namespace rftrace {
namespace {

// ---- Host mirrors of the kernel POD structs --------------------------------
// All members are 4-byte scalars, so each struct is tightly packed with 4-byte
// alignment on both the host and in OpenCL C — no float3/float4 padding. The
// static_asserts below pin the host sizes; the kernel structs use the identical
// field order and types.
struct GpuNode {
  float bmin[3];
  float bmax[3];
  std::int32_t left;    // first child index, or -1 for a leaf
  std::uint32_t start;  // leaf: first entry in the index permutation
  std::uint32_t count;  // leaf: number of triangles
};
static_assert(sizeof(GpuNode) == 36, "GpuNode layout must match the kernel");

struct GpuRay {
  float ox, oy, oz;
  float dx, dy, dz;
  float tmin, tmax;
};
static_assert(sizeof(GpuRay) == 32, "GpuRay layout must match the kernel");

struct GpuHit {
  float t, u, v;
  std::uint32_t prim;
  std::uint32_t valid;
};
static_assert(sizeof(GpuHit) == 20, "GpuHit layout must match the kernel");

// Runtime-compiled OpenCL C kernel. Two kernels share the scene buffers; the
// occlusion kernel accepts any intersection and only reports hit/miss. The
// triangle test and slab test mirror the CPU BVH exactly (Moller-Trumbore, the
// per-axis slab loop) so results agree up to float32 rounding.
const char* kKernelSource = R"CLC(
#define STACK_SIZE 64
#define TRI_EPS 1e-12f

typedef struct {
  float bmin[3];
  float bmax[3];
  int left;
  uint start;
  uint count;
} Node;

typedef struct {
  float ox, oy, oz;
  float dx, dy, dz;
  float tmin, tmax;
} Ray;

typedef struct {
  float t, u, v;
  uint prim;
  uint valid;
} HitOut;

// Per-axis slab test over [t0, t1], mirroring the CPU BVH's AABB::intersect.
bool aabbHit(float3 o, float3 d, const float* bmin, const float* bmax,
             float t0, float t1) {
  float os[3] = {o.x, o.y, o.z};
  float ds[3] = {d.x, d.y, d.z};
  for (int a = 0; a < 3; ++a) {
    float inv = 1.0f / ds[a];
    float tA = (bmin[a] - os[a]) * inv;
    float tB = (bmax[a] - os[a]) * inv;
    if (tA > tB) { float tmp = tA; tA = tB; tB = tmp; }
    t0 = tA > t0 ? tA : t0;
    t1 = tB < t1 ? tB : t1;
    if (t0 > t1) return false;
  }
  return true;
}

// Moller-Trumbore; writes t/u/v and returns 1 on a valid hit in (tmin, tmax].
int triHit(float3 ro, float3 rd, float3 v0, float3 v1, float3 v2,
           float tmin, float tmax, float* tOut, float* uOut, float* vOut) {
  float3 e1 = v1 - v0;
  float3 e2 = v2 - v0;
  float3 pv = cross(rd, e2);
  float det = dot(e1, pv);
  if (det > -TRI_EPS && det < TRI_EPS) return 0;
  float inv = 1.0f / det;
  float3 tv = ro - v0;
  float u = dot(tv, pv) * inv;
  if (u < 0.0f || u > 1.0f) return 0;
  float3 qv = cross(tv, e1);
  float v = dot(rd, qv) * inv;
  if (v < 0.0f || u + v > 1.0f) return 0;
  float t = dot(e2, qv) * inv;
  if (t < tmin || t > tmax) return 0;
  *tOut = t; *uOut = u; *vOut = v;
  return 1;
}

float3 loadVert(__global const float* verts, uint tri, int which) {
  uint base = tri * 9u + (uint)which * 3u;
  return (float3)(verts[base], verts[base + 1], verts[base + 2]);
}

__kernel void closestHit(__global const Ray* rays,
                         __global HitOut* hits,
                         const uint count,
                         __global const Node* nodes,
                         const uint nodeCount,
                         __global const float* verts,
                         __global const uint* triIdx) {
  uint gid = get_global_id(0);
  if (gid >= count) return;
  Ray r = rays[gid];
  float3 ro = (float3)(r.ox, r.oy, r.oz);
  float3 rd = (float3)(r.dx, r.dy, r.dz);

  HitOut h; h.t = 0.0f; h.u = 0.0f; h.v = 0.0f; h.prim = 0u; h.valid = 0u;
  float bestT = r.tmax;

  if (nodeCount != 0u) {
    int stack[STACK_SIZE];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
      Node nd = nodes[stack[--sp]];
      if (!aabbHit(ro, rd, nd.bmin, nd.bmax, r.tmin, bestT)) continue;
      if (nd.left < 0) {
        for (uint i = 0; i < nd.count; ++i) {
          uint tri = triIdx[nd.start + i];
          float t, u, v;
          if (triHit(ro, rd, loadVert(verts, tri, 0), loadVert(verts, tri, 1),
                     loadVert(verts, tri, 2), r.tmin, bestT, &t, &u, &v)) {
            bestT = t;
            h.t = t; h.u = u; h.v = v; h.prim = tri; h.valid = 1u;
          }
        }
      } else {
        stack[sp++] = nd.left;
        stack[sp++] = nd.left + 1;
      }
    }
  }
  hits[gid] = h;
}

__kernel void occluded(__global const Ray* rays,
                       __global HitOut* hits,
                       const uint count,
                       __global const Node* nodes,
                       const uint nodeCount,
                       __global const float* verts,
                       __global const uint* triIdx) {
  uint gid = get_global_id(0);
  if (gid >= count) return;
  Ray r = rays[gid];
  float3 ro = (float3)(r.ox, r.oy, r.oz);
  float3 rd = (float3)(r.dx, r.dy, r.dz);

  HitOut h; h.t = 0.0f; h.u = 0.0f; h.v = 0.0f; h.prim = 0u; h.valid = 0u;

  if (nodeCount != 0u) {
    int stack[STACK_SIZE];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
      Node nd = nodes[stack[--sp]];
      if (!aabbHit(ro, rd, nd.bmin, nd.bmax, r.tmin, r.tmax)) continue;
      if (nd.left < 0) {
        for (uint i = 0; i < nd.count; ++i) {
          uint tri = triIdx[nd.start + i];
          float t, u, v;
          if (triHit(ro, rd, loadVert(verts, tri, 0), loadVert(verts, tri, 1),
                     loadVert(verts, tri, 2), r.tmin, r.tmax, &t, &u, &v)) {
            h.valid = 1u;
            hits[gid] = h;
            return;
          }
        }
      } else {
        stack[sp++] = nd.left;
        stack[sp++] = nd.left + 1;
      }
    }
  }
  hits[gid] = h;
}
)CLC";

[[noreturn]] void fail(const std::string& what) {
  throw std::runtime_error("OpenCL backend: " + what);
}

void check(cl_int err, const std::string& what) {
  if (err != CL_SUCCESS)
    fail(what + " (cl error " + std::to_string(err) + ")");
}

// Find the first OpenCL GPU device across all platforms. Returns nullptr when
// none exists (never throws), so availability probing stays cheap and safe.
cl_device_id findGpuDevice() {
  cl_uint numPlatforms = 0;
  if (clGetPlatformIDs(0, nullptr, &numPlatforms) != CL_SUCCESS ||
      numPlatforms == 0)
    return nullptr;
  std::vector<cl_platform_id> platforms(numPlatforms);
  if (clGetPlatformIDs(numPlatforms, platforms.data(), nullptr) != CL_SUCCESS)
    return nullptr;
  for (cl_platform_id p : platforms) {
    cl_uint numDevices = 0;
    if (clGetDeviceIDs(p, CL_DEVICE_TYPE_GPU, 0, nullptr, &numDevices) !=
            CL_SUCCESS ||
        numDevices == 0)
      continue;
    cl_device_id dev = nullptr;
    if (clGetDeviceIDs(p, CL_DEVICE_TYPE_GPU, 1, &dev, nullptr) == CL_SUCCESS)
      return dev;
  }
  return nullptr;
}

GpuRay toGpuRay(const Ray& ray) {
  // Clamp an infinite tMax to the largest finite float the kernel can use.
  const double tmax = std::isinf(ray.tMax) ? 3.0e38 : ray.tMax;
  return GpuRay{static_cast<float>(ray.origin.x()),
                static_cast<float>(ray.origin.y()),
                static_cast<float>(ray.origin.z()),
                static_cast<float>(ray.direction.x()),
                static_cast<float>(ray.direction.y()),
                static_cast<float>(ray.direction.z()),
                static_cast<float>(ray.tMin), static_cast<float>(tmax)};
}

class OpenclBackend final : public IBackend {
 public:
  OpenclBackend() {
    device_ = findGpuDevice();
    if (device_ == nullptr) fail("no OpenCL GPU device available");

    cl_int err = CL_SUCCESS;
    context_ = clCreateContext(nullptr, 1, &device_, nullptr, nullptr, &err);
    check(err, "clCreateContext");
    queue_ = clCreateCommandQueue(context_, device_, 0, &err);
    check(err, "clCreateCommandQueue");

    const char* src = kKernelSource;
    program_ = clCreateProgramWithSource(context_, 1, &src, nullptr, &err);
    check(err, "clCreateProgramWithSource");
    err = clBuildProgram(program_, 1, &device_, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS) {
      std::size_t logSize = 0;
      clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, 0, nullptr,
                            &logSize);
      std::string log(logSize, '\0');
      if (logSize)
        clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, logSize,
                              log.data(), nullptr);
      fail("kernel build failed: " + log);
    }
    closestKernel_ = clCreateKernel(program_, "closestHit", &err);
    check(err, "clCreateKernel(closestHit)");
    occludedKernel_ = clCreateKernel(program_, "occluded", &err);
    check(err, "clCreateKernel(occluded)");
  }

  ~OpenclBackend() override {
    releaseScene();
    if (closestKernel_) clReleaseKernel(closestKernel_);
    if (occludedKernel_) clReleaseKernel(occludedKernel_);
    if (program_) clReleaseProgram(program_);
    if (queue_) clReleaseCommandQueue(queue_);
    if (context_) clReleaseContext(context_);
  }

  void build(const std::vector<Triangle>& triangles) override {
    releaseScene();
    nodeCount_ = 0;
    triangleCount_ = triangles.size();
    if (triangles.empty()) return;

    BVH bvh;
    bvh.build(triangles);
    const std::vector<BVH::FlatNode> flat = bvh.flatNodes();
    const std::vector<std::uint32_t>& perm = bvh.triangleIndices();
    const std::vector<Triangle>& tris = bvh.triangles();
    nodeCount_ = static_cast<std::uint32_t>(flat.size());

    std::vector<GpuNode> nodes(flat.size());
    for (std::size_t i = 0; i < flat.size(); ++i) {
      const BVH::FlatNode& f = flat[i];
      nodes[i] = GpuNode{{static_cast<float>(f.boxMin.x()),
                          static_cast<float>(f.boxMin.y()),
                          static_cast<float>(f.boxMin.z())},
                         {static_cast<float>(f.boxMax.x()),
                          static_cast<float>(f.boxMax.y()),
                          static_cast<float>(f.boxMax.z())},
                         f.left, f.start, f.count};
    }

    // Vertices packed as 9 floats per triangle in build order, so a hit's
    // primitive index (a value from `perm`) indexes this buffer directly.
    std::vector<float> verts;
    verts.reserve(tris.size() * 9);
    for (const Triangle& t : tris)
      for (const Vec3* v : {&t.v0, &t.v1, &t.v2}) {
        verts.push_back(static_cast<float>(v->x()));
        verts.push_back(static_cast<float>(v->y()));
        verts.push_back(static_cast<float>(v->z()));
      }

    nodeBuf_ = createBuffer(nodes.size() * sizeof(GpuNode), nodes.data());
    vertBuf_ = createBuffer(verts.size() * sizeof(float), verts.data());
    idxBuf_ = createBuffer(perm.size() * sizeof(std::uint32_t), perm.data());
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
    const std::vector<GpuHit> hits = dispatch(rays, closestKernel_);
    for (std::size_t i = 0; i < rays.size(); ++i) {
      const GpuHit& g = hits[i];
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
    const std::vector<GpuHit> hits = dispatch(rays, occludedKernel_);
    for (std::size_t i = 0; i < rays.size(); ++i)
      out[i] = hits[i].valid ? 1 : 0;
    return out;
  }

  Backend kind() const override { return Backend::OpenCL; }

 private:
  cl_mem createBuffer(std::size_t bytes, const void* host) {
    cl_int err = CL_SUCCESS;
    cl_mem buf = clCreateBuffer(
        context_, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, bytes,
        const_cast<void*>(host), &err);
    check(err, "clCreateBuffer(scene)");
    return buf;
  }

  void releaseScene() {
    if (nodeBuf_) { clReleaseMemObject(nodeBuf_); nodeBuf_ = nullptr; }
    if (vertBuf_) { clReleaseMemObject(vertBuf_); vertBuf_ = nullptr; }
    if (idxBuf_) { clReleaseMemObject(idxBuf_); idxBuf_ = nullptr; }
  }

  std::vector<GpuHit> dispatch(const std::vector<Ray>& rays,
                               cl_kernel kernel) const {
    const std::uint32_t count = static_cast<std::uint32_t>(rays.size());
    std::vector<GpuHit> result(count);
    // Empty scene (or unbuilt): every ray misses.
    if (nodeCount_ == 0 || triangleCount_ == 0) {
      std::memset(result.data(), 0, result.size() * sizeof(GpuHit));
      return result;
    }

    std::vector<GpuRay> gpuRays(count);
    for (std::uint32_t i = 0; i < count; ++i) gpuRays[i] = toGpuRay(rays[i]);

    cl_int err = CL_SUCCESS;
    cl_mem rayBuf = clCreateBuffer(
        context_, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        gpuRays.size() * sizeof(GpuRay), gpuRays.data(), &err);
    check(err, "clCreateBuffer(rays)");
    cl_mem hitBuf =
        clCreateBuffer(context_, CL_MEM_WRITE_ONLY, count * sizeof(GpuHit),
                       nullptr, &err);
    check(err, "clCreateBuffer(hits)");

    // Release the per-call buffers even if a device/driver error throws mid-way.
    try {
      cl_uint argc = 0;
      check(clSetKernelArg(kernel, argc++, sizeof(cl_mem), &rayBuf), "arg rays");
      check(clSetKernelArg(kernel, argc++, sizeof(cl_mem), &hitBuf), "arg hits");
      check(clSetKernelArg(kernel, argc++, sizeof(cl_uint), &count), "arg count");
      check(clSetKernelArg(kernel, argc++, sizeof(cl_mem), &nodeBuf_), "arg nodes");
      check(clSetKernelArg(kernel, argc++, sizeof(cl_uint), &nodeCount_),
            "arg nodeCount");
      check(clSetKernelArg(kernel, argc++, sizeof(cl_mem), &vertBuf_), "arg verts");
      check(clSetKernelArg(kernel, argc++, sizeof(cl_mem), &idxBuf_), "arg triIdx");

      const std::size_t global = count;
      err = clEnqueueNDRangeKernel(queue_, kernel, 1, nullptr, &global, nullptr,
                                   0, nullptr, nullptr);
      check(err, "clEnqueueNDRangeKernel");
      err = clEnqueueReadBuffer(queue_, hitBuf, CL_TRUE, 0,
                                count * sizeof(GpuHit), result.data(), 0, nullptr,
                                nullptr);
      check(err, "clEnqueueReadBuffer");
    } catch (...) {
      clReleaseMemObject(rayBuf);
      clReleaseMemObject(hitBuf);
      throw;
    }

    clReleaseMemObject(rayBuf);
    clReleaseMemObject(hitBuf);
    return result;
  }

  cl_device_id device_ = nullptr;
  cl_context context_ = nullptr;
  cl_command_queue queue_ = nullptr;
  cl_program program_ = nullptr;
  cl_kernel closestKernel_ = nullptr;
  cl_kernel occludedKernel_ = nullptr;
  cl_mem nodeBuf_ = nullptr;
  cl_mem vertBuf_ = nullptr;
  cl_mem idxBuf_ = nullptr;
  std::uint32_t nodeCount_ = 0;
  std::size_t triangleCount_ = 0;
};

}  // namespace

bool openclDeviceAvailable() { return findGpuDevice() != nullptr; }

std::unique_ptr<IBackend> makeOpenclBackend() {
  return std::make_unique<OpenclBackend>();
}

}  // namespace rftrace
