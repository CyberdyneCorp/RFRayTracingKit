#include <stdexcept>

#include "rftrace/backend.hpp"
#include "rftrace/bvh.hpp"

#if RFTRACE_HAVE_METAL
#include "rftrace/backends/metal_backend.hpp"
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

// Default batched implementations: loop over the single-ray queries. Backends
// that can dispatch a whole batch at once (Metal) override these.
std::vector<Hit> IBackend::closestHitBatch(const std::vector<Ray>& rays) const {
  std::vector<Hit> hits;
  hits.reserve(rays.size());
  for (const Ray& ray : rays) hits.push_back(closestHit(ray));
  return hits;
}

std::vector<char> IBackend::occludedBatch(const std::vector<Ray>& rays) const {
  std::vector<char> flags;
  flags.reserve(rays.size());
  for (const Ray& ray : rays) flags.push_back(occluded(ray) ? 1 : 0);
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
      return true;
#else
      return false;
#endif
    case Backend::Metal:
#if RFTRACE_HAVE_METAL
      return metalDeviceAvailable();
#else
      return false;
#endif
    case Backend::CUDA:
    case Backend::OpenCL:
      return false;  // not compiled in this build
  }
  return false;
}

std::unique_ptr<IBackend> makeBackend(Backend backend, bool allowFallback) {
  // The CPU traversal implementation is the reference. Embree, when enabled, is
  // validated against it in a later task; until then it maps to CPU too.
  if (backend == Backend::CPU || backend == Backend::Embree)
    return std::make_unique<CpuBackend>();

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

  if (allowFallback) return std::make_unique<CpuBackend>();
  throw std::runtime_error("backend '" + toString(backend) +
                           "' is not available in this build");
}

}  // namespace rftrace
