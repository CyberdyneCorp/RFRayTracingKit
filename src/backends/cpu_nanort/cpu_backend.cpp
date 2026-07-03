#include <stdexcept>

#include "rftrace/backend.hpp"
#include "rftrace/bvh.hpp"

#if RFTRACE_HAVE_METAL
#include "rftrace/backends/metal_backend.hpp"
#endif

#if RFTRACE_HAVE_OPENCL
#include "rftrace/backends/opencl_backend.hpp"
#endif

#if RFTRACE_HAVE_CUDA
#include "rftrace/backends/cuda_backend.hpp"
#endif

#if RFTRACE_HAVE_EMBREE
#include "rftrace/backends/embree_backend.hpp"
#endif

namespace rftrace {

namespace {

/// CPU backend: a thin adapter over the NanoRT-style BVH. This is the reference
/// implementation that all other backends are validated against.
class CpuBackend final : public IBackend {
 public:
  void build(const std::vector<Triangle>& triangles) override {
    bvh_.build(triangles);
  }
  Hit closestHit(const Ray& ray) const override { return bvh_.closestHit(ray); }
  bool occluded(const Ray& ray) const override { return bvh_.occluded(ray); }
  Backend kind() const override { return Backend::CPU; }

 private:
  BVH bvh_;
};

}  // namespace

// Caller-owned-output forms are the batched primitive: the default loops over
// the single-ray queries. Backends that can dispatch a whole batch at once
// (Metal, CUDA) override these.
void IBackend::closestHitBatchInto(const std::vector<Ray>& rays,
                                   std::span<Hit> out) const {
  for (std::size_t i = 0; i < rays.size(); ++i) out[i] = closestHit(rays[i]);
}

void IBackend::occludedBatchInto(const std::vector<Ray>& rays,
                                 std::span<char> out) const {
  for (std::size_t i = 0; i < rays.size(); ++i)
    out[i] = occluded(rays[i]) ? 1 : 0;
}

// Vector-returning forms: allocate once, then delegate to the Into primitive so
// a batch backend's single-dispatch override is used here too.
std::vector<Hit> IBackend::closestHitBatch(const std::vector<Ray>& rays) const {
  std::vector<Hit> hits(rays.size());
  closestHitBatchInto(rays, hits);
  return hits;
}

std::vector<char> IBackend::occludedBatch(const std::vector<Ray>& rays) const {
  std::vector<char> flags(rays.size());
  occludedBatchInto(rays, flags);
  return flags;
}

std::string toString(Backend backend) {
  switch (backend) {
    case Backend::CPU: return "cpu";
    case Backend::Embree: return "embree";
    case Backend::Metal: return "metal";
    case Backend::CUDA: return "cuda";
    case Backend::OpenCL: return "opencl";
  }
  return "cpu";
}

Backend backendFromString(const std::string& name) {
  if (name == "embree") return Backend::Embree;
  if (name == "metal") return Backend::Metal;
  if (name == "cuda") return Backend::CUDA;
  if (name == "opencl") return Backend::OpenCL;
  return Backend::CPU;
}

bool backendAvailable(Backend backend) {
  switch (backend) {
    case Backend::CPU:
      return true;
    case Backend::Embree:
#if RFTRACE_HAVE_EMBREE
      return embreeDeviceAvailable();
#else
      return false;
#endif
    case Backend::Metal:
#if RFTRACE_HAVE_METAL
      return metalDeviceAvailable();
#else
      return false;
#endif
    case Backend::OpenCL:
#if RFTRACE_HAVE_OPENCL
      return openclDeviceAvailable();
#else
      return false;
#endif
    case Backend::CUDA:
#if RFTRACE_HAVE_CUDA
      return cudaDeviceAvailable();
#else
      return false;
#endif
  }
  return false;
}

std::unique_ptr<IBackend> makeBackend(Backend backend, bool allowFallback) {
  // The CPU traversal implementation is the reference and default fallback.
  if (backend == Backend::CPU)
    return std::make_unique<CpuBackend>();

#if RFTRACE_HAVE_EMBREE
  if (backend == Backend::Embree && embreeDeviceAvailable()) {
    try {
      return makeEmbreeBackend();
    } catch (const std::exception&) {
      // Runtime device/scene-build failure: treat the backend as unavailable and
      // fall back to CPU when permitted, rather than propagating.
      if (!allowFallback) throw;
    }
  }
#endif

#if RFTRACE_HAVE_METAL
  if (backend == Backend::Metal && metalDeviceAvailable()) {
    try {
      return makeMetalBackend();
    } catch (const std::exception&) {
      // Runtime kernel/AS build failure: treat the backend as unavailable and
      // fall back to CPU when permitted, rather than propagating.
      if (!allowFallback) throw;
    }
  }
#endif

#if RFTRACE_HAVE_OPENCL
  if (backend == Backend::OpenCL && openclDeviceAvailable()) {
    try {
      return makeOpenclBackend();
    } catch (const std::exception&) {
      // Runtime kernel-compile/device failure: treat the backend as unavailable
      // and fall back to CPU when permitted, rather than propagating.
      if (!allowFallback) throw;
    }
  }
#endif

#if RFTRACE_HAVE_CUDA
  if (backend == Backend::CUDA && cudaDeviceAvailable()) {
    try {
      return makeCudaBackend();
    } catch (const std::exception&) {
      // Runtime driver/OptiX/PTX/AS-build failure: treat the backend as
      // unavailable and fall back to CPU when permitted, rather than propagating.
      if (!allowFallback) throw;
    }
  }
#endif

  if (allowFallback) return std::make_unique<CpuBackend>();
  throw std::runtime_error("backend '" + toString(backend) +
                           "' is not available in this build");
}

}  // namespace rftrace
