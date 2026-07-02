#pragma once

// Factory for the Metal GPU ray-tracing backend. Only declared (and defined)
// when the Metal backend is compiled in (RFTRACE_HAVE_METAL, set by CMake when
// RFTRACE_ENABLE_METAL is on and the platform is Apple). Keeping the factory
// behind this guard lets the rest of the code stay backend-agnostic.

#include <memory>

#include "rftrace/backend.hpp"

namespace rftrace {

#if RFTRACE_HAVE_METAL

/// Create the Metal hardware ray-tracing backend. Throws std::runtime_error if
/// no Metal device is available at runtime. Prefer makeBackend(Backend::Metal)
/// which handles availability and fallback.
std::unique_ptr<IBackend> makeMetalBackend();

/// True if a Metal device with ray-tracing support exists at runtime.
bool metalDeviceAvailable();

#endif  // RFTRACE_HAVE_METAL

}  // namespace rftrace
