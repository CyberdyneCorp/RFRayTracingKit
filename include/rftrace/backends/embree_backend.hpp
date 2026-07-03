#pragma once

// Factory for the optional Embree CPU ray-tracing backend. Only declared (and
// defined) when the Embree backend is compiled in (RFTRACE_HAVE_EMBREE, set by
// CMake when RFTRACE_ENABLE_EMBREE is on and Embree 4 is found). Keeping the
// factory behind this guard lets the rest of the code stay backend-agnostic,
// exactly like cuda_backend.hpp / opencl_backend.hpp.
//
// Embree is a high-performance CPU ray tracer (Intel). This backend intersects
// rays against an Embree RTCScene built from the scene triangles; it serves as a
// fast, portable validation backend for the double-precision CPU BVH reference.

#include <memory>

#include "rftrace/backend.hpp"

namespace rftrace {

#if RFTRACE_HAVE_EMBREE

/// Create the Embree CPU ray-traversal backend. Throws std::runtime_error if an
/// Embree device cannot be created at runtime. Prefer makeBackend(Backend::Embree)
/// which handles availability and CPU fallback.
std::unique_ptr<IBackend> makeEmbreeBackend();

/// True if an Embree device can be created (and released) at runtime.
bool embreeDeviceAvailable();

#endif  // RFTRACE_HAVE_EMBREE

}  // namespace rftrace
