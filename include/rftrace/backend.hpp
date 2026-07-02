#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rftrace/geometry.hpp"

namespace rftrace {

/// Acceleration backend selector. Only CPU (and optionally Embree) are available
/// in Phase 1; Metal/CUDA/OpenCL are declared so the API is stable for later
/// phases.
enum class Backend { CPU, Embree, Metal, CUDA, OpenCL };

std::string toString(Backend backend);

/// Parse a backend name (e.g. "cpu", "metal"); defaults to CPU when unknown.
Backend backendFromString(const std::string& name);

/// True if the backend was compiled into this build.
bool backendAvailable(Backend backend);

/// Ray-traversal contract that isolates hardware acceleration from the scene,
/// RF physics, and result code. Implementations own only the acceleration
/// structure and its queries.
class IBackend {
 public:
  virtual ~IBackend() = default;
  virtual void build(const std::vector<Triangle>& triangles) = 0;
  virtual Hit closestHit(const Ray& ray) const = 0;
  virtual bool occluded(const Ray& ray) const = 0;
  virtual Backend kind() const = 0;
};

/// Create a backend. If the requested backend is unavailable and
/// `allowFallback` is true, returns the CPU backend; otherwise throws.
std::unique_ptr<IBackend> makeBackend(Backend backend,
                                      bool allowFallback = true);

}  // namespace rftrace
