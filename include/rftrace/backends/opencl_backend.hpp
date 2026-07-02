#pragma once

// Factory for the OpenCL GPU ray-tracing backend. Only declared (and defined)
// when the OpenCL backend is compiled in (RFTRACE_HAVE_OPENCL, set by CMake when
// RFTRACE_ENABLE_OPENCL is on and find_package(OpenCL) succeeds). Keeping the
// factory behind this guard lets the rest of the code stay backend-agnostic.
//
// Unlike Metal/CUDA there is no hardware ray tracing in OpenCL 1.2, so this
// backend traverses a custom flat BVH with a runtime-compiled OpenCL C kernel.

#include <memory>

#include "rftrace/backend.hpp"

namespace rftrace {

#if RFTRACE_HAVE_OPENCL

/// Create the OpenCL ray-traversal backend. Throws std::runtime_error if no
/// OpenCL device is available or the kernel fails to compile at runtime. Prefer
/// makeBackend(Backend::OpenCL) which handles availability and CPU fallback.
std::unique_ptr<IBackend> makeOpenclBackend();

/// True if an OpenCL platform with a usable GPU device exists at runtime.
bool openclDeviceAvailable();

#endif  // RFTRACE_HAVE_OPENCL

}  // namespace rftrace
