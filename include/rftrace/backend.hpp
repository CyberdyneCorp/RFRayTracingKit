#pragma once

#include <memory>
#include <span>
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
///
/// Threading: after build(), the query methods are safe to call from a single
/// thread. They are NOT guaranteed reentrant across threads on GPU backends
/// (which reuse one command queue and per-call buffers) — a caller wanting
/// concurrency should use one backend instance per thread.
///
/// Performance note: GPU backends pay a host<->device round trip per call, so
/// per-ray closestHit()/occluded() on Metal is correct but slow; prefer the
/// batched methods below. The Phase 1 image-method simulator still issues
/// per-ray queries, so today Metal is primarily a validated, batch-capable
/// backend rather than a faster drop-in for that path.
class IBackend {
 public:
  virtual ~IBackend() = default;
  virtual void build(const std::vector<Triangle>& triangles) = 0;
  virtual Hit closestHit(const Ray& ray) const = 0;
  virtual bool occluded(const Ray& ray) const = 0;
  virtual Backend kind() const = 0;

  /// Batched closest-hit query. Allocates and returns one `Hit` per ray,
  /// index-aligned with `rays`. Thin wrapper over `closestHitBatchInto`.
  virtual std::vector<Hit> closestHitBatch(const std::vector<Ray>& rays) const;

  /// Batched occlusion query. `char` (not `bool`) so the result is a contiguous
  /// byte buffer that maps cleanly to GPU storage. Results are index-aligned
  /// with `rays`. Thin wrapper over `occludedBatchInto`.
  virtual std::vector<char> occludedBatch(const std::vector<Ray>& rays) const;

  /// Caller-owned-output batched closest-hit: write one `Hit` per ray into `out`
  /// (`out.size()` must equal `rays.size()`), index-aligned with `rays`. Unlike
  /// `closestHitBatch`, this allocates nothing for the output, so a hot query
  /// loop can reuse a single buffer across many batches. This is the primitive
  /// accelerated backends override with a single device dispatch; the default
  /// implementation loops over `closestHit()`.
  virtual void closestHitBatchInto(const std::vector<Ray>& rays,
                                   std::span<Hit> out) const;

  /// Caller-owned-output batched occlusion query; see `closestHitBatchInto`.
  virtual void occludedBatchInto(const std::vector<Ray>& rays,
                                 std::span<char> out) const;
};

/// Create a backend. If the requested backend is unavailable and
/// `allowFallback` is true, returns the CPU backend; otherwise throws.
std::unique_ptr<IBackend> makeBackend(Backend backend,
                                      bool allowFallback = true);

}  // namespace rftrace
