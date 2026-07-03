// CUDA/OptiX GPU ray-tracing backend (host side). Compiled ONLY when
// RFTRACE_ENABLE_CUDA is on and both the CUDA Toolkit and the OptiX SDK are
// found (RFTRACE_HAVE_CUDA). Builds an OptiX geometry acceleration structure
// (GAS) from the scene triangles and intersects batches of rays with a single
// optixLaunch over device programs compiled to PTX (see cuda_programs.cu). RF
// physics stays double-precision on the host; only the acceleration structure
// and the ray/hit buffers crossing to the device are float32 — the same
// boundary as the Metal and OpenCL backends.
//
// ============================================================================
// VERIFIED on NVIDIA hardware: compiled and run on a GeForce RTX 5060 (Blackwell,
// sm_120) with CUDA Toolkit 12.0, driver 580.95.05, and OptiX SDK 9.0.0 — the
// CPU-vs-CUDA parity suite (test_cuda_parity.cpp) passes. Written to the OptiX
// 7.7/8 host API (no 9.x-only calls) and mirrors the Metal backend. The OptiX SDK
// version must have an ABI the installed driver implements, or optixInit() fails
// with OPTIX_ERROR_UNSUPPORTED_ABI_VERSION (see README). The default build (CUDA
// OFF) never compiles this file.
// ============================================================================

#include <optix.h>
#include <optix_function_table_definition.h>  // defines g_optixFunctionTable (exactly one TU)
#include <optix_stack_size.h>
#include <optix_stubs.h>

#include <cuda_runtime.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cuda_shared.h"
#include "rftrace/backend.hpp"
#include "rftrace/backends/cuda_backend.hpp"

// Absolute path of the PTX produced from cuda_programs.cu. The build system
// generates this header (via file(GENERATE) + $<TARGET_OBJECTS>) so the runtime
// module loader knows where the compiled device program lives.
#include "rftrace_cuda_ptx_path.h"
#ifndef RFTRACE_CUDA_PTX_FILE
#error "RFTRACE_CUDA_PTX_FILE must be defined by the build system"
#endif

namespace rftrace {
namespace {

using cuda::GpuHit;
using cuda::GpuRay;
using cuda::LaunchParams;

[[noreturn]] void fail(const std::string& what) {
  throw std::runtime_error("CUDA backend: " + what);
}

void cudaCheck(cudaError_t err, const std::string& what) {
  if (err != cudaSuccess)
    fail(what + " (cuda error: " + cudaGetErrorString(err) + ")");
}

void optixCheck(OptixResult res, const std::string& what) {
  if (res != OPTIX_SUCCESS)
    fail(what + " (optix error " + std::to_string(static_cast<int>(res)) + ")");
}

// Round `n` up to the next multiple of `a` (a is a power of two).
std::size_t alignUp(std::size_t n, std::size_t a) {
  return (n + a - 1) & ~(a - 1);
}

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) fail("cannot open PTX file: " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// SBT record: OptiX-required header followed by (unused) per-record data.
template <typename T>
struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) SbtRecord {
  char header[OPTIX_SBT_RECORD_HEADER_SIZE];
  T data;
};
struct EmptyData {
  int unused = 0;
};
using RayGenRecord = SbtRecord<EmptyData>;
using MissRecord = SbtRecord<EmptyData>;
using HitGroupRecord = SbtRecord<EmptyData>;

GpuRay toGpuRay(const Ray& ray) {
  // Clamp an infinite tMax to the largest finite float the device can use.
  const double tmax = std::isinf(ray.tMax) ? 3.0e38 : ray.tMax;
  return GpuRay{static_cast<float>(ray.origin.x()),
                static_cast<float>(ray.origin.y()),
                static_cast<float>(ray.origin.z()),
                static_cast<float>(ray.direction.x()),
                static_cast<float>(ray.direction.y()),
                static_cast<float>(ray.direction.z()),
                static_cast<float>(ray.tMin), static_cast<float>(tmax)};
}

class CudaBackend final : public IBackend {
 public:
  CudaBackend() {
    initContext();
    buildPipeline();
    buildSbt();
    cudaCheck(cudaStreamCreate(&stream_), "cudaStreamCreate");
    cudaCheck(cudaMalloc(reinterpret_cast<void**>(&paramsBuf_),
                         sizeof(LaunchParams)),
              "cudaMalloc(params)");
  }

  ~CudaBackend() override {
    releaseScene();
    if (rayBuf_) cudaFree(reinterpret_cast<void*>(rayBuf_));
    if (hitBuf_) cudaFree(reinterpret_cast<void*>(hitBuf_));
    if (paramsBuf_) cudaFree(reinterpret_cast<void*>(paramsBuf_));
    if (raygenRecord_) cudaFree(reinterpret_cast<void*>(raygenRecord_));
    if (missRecord_) cudaFree(reinterpret_cast<void*>(missRecord_));
    if (hitRecord_) cudaFree(reinterpret_cast<void*>(hitRecord_));
    if (pipeline_) optixPipelineDestroy(pipeline_);
    if (raygenPG_) optixProgramGroupDestroy(raygenPG_);
    if (missPG_) optixProgramGroupDestroy(missPG_);
    if (hitPG_) optixProgramGroupDestroy(hitPG_);
    if (module_) optixModuleDestroy(module_);
    if (context_) optixDeviceContextDestroy(context_);
    if (stream_) cudaStreamDestroy(stream_);
  }

  void build(const std::vector<Triangle>& triangles) override {
    releaseScene();
    triangleCount_ = triangles.size();
    handle_ = 0;
    if (triangles.empty()) return;

    // Pack vertices as float3 triplets (one triangle per 3 vertices) with an
    // identity index buffer, so the OptiX primitive index equals our triangle
    // index — mirrors the Metal backend.
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

    cudaCheck(cudaMalloc(reinterpret_cast<void**>(&vertBuf_),
                         verts.size() * sizeof(float)),
              "cudaMalloc(verts)");
    cudaCheck(cudaMemcpy(reinterpret_cast<void*>(vertBuf_), verts.data(),
                         verts.size() * sizeof(float), cudaMemcpyHostToDevice),
              "cudaMemcpy(verts)");
    cudaCheck(cudaMalloc(reinterpret_cast<void**>(&idxBuf_),
                         indices.size() * sizeof(std::uint32_t)),
              "cudaMalloc(indices)");
    cudaCheck(cudaMemcpy(reinterpret_cast<void*>(idxBuf_), indices.data(),
                         indices.size() * sizeof(std::uint32_t),
                         cudaMemcpyHostToDevice),
              "cudaMemcpy(indices)");

    buildGas(static_cast<std::uint32_t>(triangleCount_));
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
    const std::vector<GpuHit> hits = dispatch(rays, /*occlusion=*/false);
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
    const std::vector<GpuHit> hits = dispatch(rays, /*occlusion=*/true);
    for (std::size_t i = 0; i < rays.size(); ++i)
      out[i] = hits[i].valid ? 1 : 0;
    return out;
  }

  Backend kind() const override { return Backend::CUDA; }

 private:
  void initContext() {
    int deviceCount = 0;
    cudaCheck(cudaGetDeviceCount(&deviceCount), "cudaGetDeviceCount");
    if (deviceCount == 0) fail("no CUDA device present");
    cudaCheck(cudaFree(nullptr), "cuda context init");  // force context creation

    optixCheck(optixInit(), "optixInit");
    OptixDeviceContextOptions opts = {};
    opts.logCallbackLevel = 0;
    optixCheck(optixDeviceContextCreate(/*fromCurrent=*/nullptr, &opts,
                                        &context_),
               "optixDeviceContextCreate");
  }

  void buildPipeline() {
    OptixModuleCompileOptions moduleOpts = {};
    moduleOpts.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
    moduleOpts.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    moduleOpts.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_NONE;

    pipelineCompileOpts_ = {};
    pipelineCompileOpts_.usesMotionBlur = 0;
    pipelineCompileOpts_.traversableGraphFlags =
        OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;
    pipelineCompileOpts_.numPayloadValues = 5;   // valid,t,prim,u,v
    pipelineCompileOpts_.numAttributeValues = 2;  // built-in triangle barycentrics
    pipelineCompileOpts_.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pipelineCompileOpts_.pipelineLaunchParamsVariableName = "params";
    pipelineCompileOpts_.usesPrimitiveTypeFlags =
        OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;

    const std::string ptx = readFile(RFTRACE_CUDA_PTX_FILE);
    char log[4096];
    std::size_t logSize = sizeof(log);
    optixCheck(optixModuleCreate(context_, &moduleOpts, &pipelineCompileOpts_,
                                 ptx.c_str(), ptx.size(), log, &logSize,
                                 &module_),
               "optixModuleCreate");

    OptixProgramGroupOptions pgOpts = {};

    OptixProgramGroupDesc raygenDesc = {};
    raygenDesc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    raygenDesc.raygen.module = module_;
    raygenDesc.raygen.entryFunctionName = "__raygen__rg";
    logSize = sizeof(log);
    optixCheck(optixProgramGroupCreate(context_, &raygenDesc, 1, &pgOpts, log,
                                       &logSize, &raygenPG_),
               "optixProgramGroupCreate(raygen)");

    OptixProgramGroupDesc missDesc = {};
    missDesc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    missDesc.miss.module = module_;
    missDesc.miss.entryFunctionName = "__miss__ms";
    logSize = sizeof(log);
    optixCheck(optixProgramGroupCreate(context_, &missDesc, 1, &pgOpts, log,
                                       &logSize, &missPG_),
               "optixProgramGroupCreate(miss)");

    OptixProgramGroupDesc hitDesc = {};
    hitDesc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    hitDesc.hitgroup.moduleCH = module_;
    hitDesc.hitgroup.entryFunctionNameCH = "__closesthit__ch";
    logSize = sizeof(log);
    optixCheck(optixProgramGroupCreate(context_, &hitDesc, 1, &pgOpts, log,
                                       &logSize, &hitPG_),
               "optixProgramGroupCreate(hitgroup)");

    OptixProgramGroup groups[] = {raygenPG_, missPG_, hitPG_};
    OptixPipelineLinkOptions linkOpts = {};
    linkOpts.maxTraceDepth = 1;
    logSize = sizeof(log);
    optixCheck(optixPipelineCreate(context_, &pipelineCompileOpts_, &linkOpts,
                                   groups, 3, log, &logSize, &pipeline_),
               "optixPipelineCreate");

    // Conservative stack sizing for a single trace-depth pipeline.
    OptixStackSizes stackSizes = {};
    for (OptixProgramGroup pg : groups)
#if OPTIX_VERSION >= 70700
      optixCheck(optixUtilAccumulateStackSizes(pg, &stackSizes, pipeline_),
                 "optixUtilAccumulateStackSizes");
#else
      optixCheck(optixUtilAccumulateStackSizes(pg, &stackSizes),
                 "optixUtilAccumulateStackSizes");
#endif
    std::uint32_t dcTraversal = 0, dcState = 0, continuation = 0;
    optixCheck(optixUtilComputeStackSizes(&stackSizes, /*maxTraceDepth=*/1,
                                          /*maxCCDepth=*/0, /*maxDCDepth=*/0,
                                          &dcTraversal, &dcState, &continuation),
               "optixUtilComputeStackSizes");
    optixCheck(optixPipelineSetStackSize(pipeline_, dcTraversal, dcState,
                                         continuation, /*maxTraversableDepth=*/1),
               "optixPipelineSetStackSize");
  }

  void buildSbt() {
    RayGenRecord rg = {};
    optixCheck(optixSbtRecordPackHeader(raygenPG_, &rg), "packHeader(raygen)");
    MissRecord ms = {};
    optixCheck(optixSbtRecordPackHeader(missPG_, &ms), "packHeader(miss)");
    HitGroupRecord hg = {};
    optixCheck(optixSbtRecordPackHeader(hitPG_, &hg), "packHeader(hit)");

    raygenRecord_ = uploadRecord(&rg, sizeof(rg));
    missRecord_ = uploadRecord(&ms, sizeof(ms));
    hitRecord_ = uploadRecord(&hg, sizeof(hg));

    sbt_ = {};
    sbt_.raygenRecord = raygenRecord_;
    sbt_.missRecordBase = missRecord_;
    sbt_.missRecordStrideInBytes = sizeof(MissRecord);
    sbt_.missRecordCount = 1;
    sbt_.hitgroupRecordBase = hitRecord_;
    sbt_.hitgroupRecordStrideInBytes = sizeof(HitGroupRecord);
    sbt_.hitgroupRecordCount = 1;
  }

  CUdeviceptr uploadRecord(const void* host, std::size_t bytes) {
    CUdeviceptr d = 0;
    cudaCheck(cudaMalloc(reinterpret_cast<void**>(&d), bytes),
              "cudaMalloc(sbt record)");
    cudaCheck(cudaMemcpy(reinterpret_cast<void*>(d), host, bytes,
                         cudaMemcpyHostToDevice),
              "cudaMemcpy(sbt record)");
    return d;
  }

  void buildGas(std::uint32_t triangleCount) {
    OptixBuildInput input = {};
    input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    input.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
    input.triangleArray.vertexStrideInBytes = 3 * sizeof(float);
    input.triangleArray.numVertices = triangleCount * 3;
    input.triangleArray.vertexBuffers = &vertBuf_;
    input.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    input.triangleArray.indexStrideInBytes = 3 * sizeof(std::uint32_t);
    input.triangleArray.numIndexTriplets = triangleCount;
    input.triangleArray.indexBuffer = idxBuf_;
    const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
    input.triangleArray.flags = flags;
    input.triangleArray.numSbtRecords = 1;

    OptixAccelBuildOptions accelOpts = {};
    accelOpts.buildFlags =
        OPTIX_BUILD_FLAG_ALLOW_COMPACTION | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    accelOpts.operation = OPTIX_BUILD_OPERATION_BUILD;

    OptixAccelBufferSizes sizes = {};
    optixCheck(optixAccelComputeMemoryUsage(context_, &accelOpts, &input, 1,
                                            &sizes),
               "optixAccelComputeMemoryUsage");

    CUdeviceptr tempBuf = 0;
    cudaCheck(cudaMalloc(reinterpret_cast<void**>(&tempBuf),
                         sizes.tempSizeInBytes),
              "cudaMalloc(gas temp)");
    CUdeviceptr outputBuf = 0;
    cudaCheck(cudaMalloc(reinterpret_cast<void**>(&outputBuf),
                         sizes.outputSizeInBytes),
              "cudaMalloc(gas output)");

    // Request the compacted size via a device-side emit property.
    CUdeviceptr compactedSizeBuf = 0;
    cudaCheck(cudaMalloc(reinterpret_cast<void**>(&compactedSizeBuf),
                         sizeof(std::uint64_t)),
              "cudaMalloc(compacted size)");
    OptixAccelEmitDesc emit = {};
    emit.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
    emit.result = compactedSizeBuf;

    optixCheck(optixAccelBuild(context_, stream_, &accelOpts, &input, 1, tempBuf,
                               sizes.tempSizeInBytes, outputBuf,
                               sizes.outputSizeInBytes, &handle_, &emit, 1),
               "optixAccelBuild");
    cudaCheck(cudaStreamSynchronize(stream_), "cudaStreamSynchronize(build)");

    std::uint64_t compactedSize = 0;
    cudaCheck(cudaMemcpy(&compactedSize,
                         reinterpret_cast<void*>(compactedSizeBuf),
                         sizeof(std::uint64_t), cudaMemcpyDeviceToHost),
              "cudaMemcpy(compacted size)");
    cudaFree(reinterpret_cast<void*>(compactedSizeBuf));
    cudaFree(reinterpret_cast<void*>(tempBuf));

    if (compactedSize > 0 && compactedSize < sizes.outputSizeInBytes) {
      cudaCheck(cudaMalloc(reinterpret_cast<void**>(&gasBuf_), compactedSize),
                "cudaMalloc(gas compacted)");
      optixCheck(optixAccelCompact(context_, stream_, handle_, gasBuf_,
                                   compactedSize, &handle_),
                 "optixAccelCompact");
      cudaCheck(cudaStreamSynchronize(stream_),
                "cudaStreamSynchronize(compact)");
      cudaFree(reinterpret_cast<void*>(outputBuf));
    } else {
      gasBuf_ = outputBuf;  // keep the uncompacted buffer alive
    }
  }

  std::vector<GpuHit> dispatch(const std::vector<Ray>& rays,
                               bool occlusion) const {
    const std::uint32_t count = static_cast<std::uint32_t>(rays.size());
    std::vector<GpuHit> result(count);
    // Empty scene (or unbuilt): every ray misses.
    if (handle_ == 0 || triangleCount_ == 0) {
      std::memset(result.data(), 0, result.size() * sizeof(GpuHit));
      return result;
    }

    std::vector<GpuRay> gpuRays(count);
    for (std::uint32_t i = 0; i < count; ++i) gpuRays[i] = toGpuRay(rays[i]);

    ensureQueryCapacity(count);
    cudaCheck(cudaMemcpy(reinterpret_cast<void*>(rayBuf_), gpuRays.data(),
                         count * sizeof(GpuRay), cudaMemcpyHostToDevice),
              "cudaMemcpy(rays)");

    LaunchParams params = {};
    params.rays = reinterpret_cast<const GpuRay*>(rayBuf_);
    params.hits = reinterpret_cast<GpuHit*>(hitBuf_);
    params.count = count;
    params.occlusion = occlusion ? 1u : 0u;
    params.handle = static_cast<unsigned long long>(handle_);
    cudaCheck(cudaMemcpy(reinterpret_cast<void*>(paramsBuf_), &params,
                         sizeof(LaunchParams), cudaMemcpyHostToDevice),
              "cudaMemcpy(params)");

    optixCheck(optixLaunch(pipeline_, stream_, paramsBuf_, sizeof(LaunchParams),
                           &sbt_, count, 1, 1),
               "optixLaunch");
    cudaCheck(cudaStreamSynchronize(stream_), "cudaStreamSynchronize(launch)");

    cudaCheck(cudaMemcpy(result.data(), reinterpret_cast<void*>(hitBuf_),
                         count * sizeof(GpuHit), cudaMemcpyDeviceToHost),
              "cudaMemcpy(hits)");
    return result;
  }

  // Grow the reusable device ray/hit buffers to hold at least `count` rays.
  // Pooled across dispatch() calls (grown on demand, never shrunk) so a steady
  // batch size allocates once instead of cudaMalloc/cudaFree per launch.
  void ensureQueryCapacity(std::size_t count) const {
    if (count <= rayCapacity_) return;
    if (rayBuf_) cudaFree(reinterpret_cast<void*>(rayBuf_));
    if (hitBuf_) cudaFree(reinterpret_cast<void*>(hitBuf_));
    rayBuf_ = 0;
    hitBuf_ = 0;
    rayCapacity_ = 0;  // stay consistent if a malloc below throws
    cudaCheck(cudaMalloc(reinterpret_cast<void**>(&rayBuf_),
                         count * sizeof(GpuRay)),
              "cudaMalloc(rays)");
    cudaCheck(cudaMalloc(reinterpret_cast<void**>(&hitBuf_),
                         count * sizeof(GpuHit)),
              "cudaMalloc(hits)");
    rayCapacity_ = count;
  }

  void releaseScene() {
    if (vertBuf_) { cudaFree(reinterpret_cast<void*>(vertBuf_)); vertBuf_ = 0; }
    if (idxBuf_) { cudaFree(reinterpret_cast<void*>(idxBuf_)); idxBuf_ = 0; }
    if (gasBuf_) { cudaFree(reinterpret_cast<void*>(gasBuf_)); gasBuf_ = 0; }
    handle_ = 0;
    triangleCount_ = 0;
  }

  // Context / pipeline (built once).
  OptixDeviceContext context_ = nullptr;
  OptixModule module_ = nullptr;
  OptixProgramGroup raygenPG_ = nullptr;
  OptixProgramGroup missPG_ = nullptr;
  OptixProgramGroup hitPG_ = nullptr;
  OptixPipeline pipeline_ = nullptr;
  OptixPipelineCompileOptions pipelineCompileOpts_ = {};
  OptixShaderBindingTable sbt_ = {};
  CUstream stream_ = nullptr;

  CUdeviceptr raygenRecord_ = 0;
  CUdeviceptr missRecord_ = 0;
  CUdeviceptr hitRecord_ = 0;
  CUdeviceptr paramsBuf_ = 0;

  // Reusable per-query device buffers, pooled across dispatch() calls (grown on
  // demand, never shrunk). Mutable because dispatch()/ensureQueryCapacity() are
  // const; safe under the documented single-threaded query contract.
  mutable CUdeviceptr rayBuf_ = 0;
  mutable CUdeviceptr hitBuf_ = 0;
  mutable std::size_t rayCapacity_ = 0;

  // Scene (rebuilt on each build()).
  CUdeviceptr vertBuf_ = 0;
  CUdeviceptr idxBuf_ = 0;
  CUdeviceptr gasBuf_ = 0;
  OptixTraversableHandle handle_ = 0;
  std::size_t triangleCount_ = 0;
};

}  // namespace

bool cudaDeviceAvailable() {
  int deviceCount = 0;
  if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount == 0)
    return false;
  // optixInit loads the OptiX function table via the driver; treat any failure
  // as "unavailable" so makeBackend falls back to CPU rather than throwing.
  return optixInit() == OPTIX_SUCCESS;
}

std::unique_ptr<IBackend> makeCudaBackend() {
  return std::make_unique<CudaBackend>();
}

}  // namespace rftrace
