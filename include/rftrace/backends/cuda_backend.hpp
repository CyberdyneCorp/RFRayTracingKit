#pragma once

// Factory for the CUDA/OptiX GPU ray-tracing backend. Only declared (and
// defined) when the CUDA backend is compiled in (RFTRACE_HAVE_CUDA, set by CMake
// when RFTRACE_ENABLE_CUDA is on and both the CUDA Toolkit and OptiX SDK are
// found). Keeping the factory behind this guard lets the rest of the code stay
// backend-agnostic, exactly like metal_backend.hpp / opencl_backend.hpp.
//
// NOTE: this backend is UNVERIFIED on non-NVIDIA hosts — it is authored to the
// CUDA/OptiX API and compiles only where a CUDA Toolkit + OptiX SDK exist. It
// has not been compiled or run on the (Apple) development host.

#include <memory>

#include "rftrace/backend.hpp"

namespace rftrace {

#if RFTRACE_HAVE_CUDA

/// Create the CUDA/OptiX hardware ray-tracing backend. Throws std::runtime_error
/// if no CUDA device is available or OptiX/PTX initialisation fails at runtime.
/// Prefer makeBackend(Backend::CUDA) which handles availability and CPU fallback.
std::unique_ptr<IBackend> makeCudaBackend();

/// True if a CUDA device exists and OptiX initialises successfully at runtime.
bool cudaDeviceAvailable();

#endif  // RFTRACE_HAVE_CUDA

}  // namespace rftrace
