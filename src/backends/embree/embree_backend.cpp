// Embree CPU ray-tracing backend (Intel Embree 4). Compiled ONLY when
// RFTRACE_ENABLE_EMBREE is on and Embree 4 is found (RFTRACE_HAVE_EMBREE).
// Builds an Embree RTCScene from the scene triangles and intersects rays with
// rtcIntersect1 / rtcOccluded1. RF physics stays double-precision on the host;
// only the acceleration structure vertices and the per-ray org/dir/tnear/tfar
// crossing into Embree are float32 — the same double->float boundary as the
// Metal, CUDA, and OpenCL backends. It serves as a fast, portable validation
// backend for the CPU BVH reference. The default build (Embree OFF) never
// compiles this file.

#include <embree4/rtcore.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "rftrace/backend.hpp"
#include "rftrace/backends/embree_backend.hpp"

namespace rftrace {
namespace {

[[noreturn]] void fail(const std::string& what) {
  throw std::runtime_error("Embree backend: " + what);
}

// Surface Embree device/commit errors on stderr so build failures are visible.
void errorCallback(void* /*userPtr*/, RTCError code, const char* str) {
  std::fprintf(stderr, "[embree] error %d: %s\n", static_cast<int>(code),
               str ? str : "");
}

// Clamp an infinite tMax to the largest finite float the intersector can use —
// the same inf clamp as toGpuRay() in cuda_backend.cpp.
float clampTFar(double tMax) {
  return std::isinf(tMax) ? 3.0e38f : static_cast<float>(tMax);
}

class EmbreeBackend final : public IBackend {
 public:
  EmbreeBackend() {
    device_ = rtcNewDevice(nullptr);
    if (!device_) fail("rtcNewDevice failed");
    rtcSetDeviceErrorFunction(device_, &errorCallback, nullptr);
  }

  ~EmbreeBackend() override {
    releaseScene();
    if (device_) rtcReleaseDevice(device_);
  }

  void build(const std::vector<Triangle>& triangles) override {
    releaseScene();
    triangleCount_ = triangles.size();

    scene_ = rtcNewScene(device_);
    rtcSetSceneFlags(scene_, RTC_SCENE_FLAG_ROBUST);
    // An empty committed scene is valid and returns misses, so the query path
    // needs no null/empty guard.
    if (!triangles.empty()) {
      RTCGeometry geom =
          rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_TRIANGLE);

      // Vertices: one triangle per 3 consecutive vertices. Embree owns and
      // 16-byte-pads this allocation; FLOAT3's documented stride is 12 bytes.
      auto* verts = static_cast<float*>(rtcSetNewGeometryBuffer(
          geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3,
          3 * sizeof(float), 3 * triangleCount_));
      // Indices: identity triplet N = (3N, 3N+1, 3N+2), so primID == N == the
      // input Triangle index (single geometry => geomID 0, no remapping).
      auto* idx = static_cast<std::uint32_t*>(rtcSetNewGeometryBuffer(
          geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3,
          3 * sizeof(std::uint32_t), triangleCount_));
      if (!verts || !idx) {
        rtcReleaseGeometry(geom);
        fail("rtcSetNewGeometryBuffer failed");
      }

      std::uint32_t vi = 0;
      for (const Triangle& t : triangles) {
        for (const Vec3* v : {&t.v0, &t.v1, &t.v2}) {
          verts[vi * 3 + 0] = static_cast<float>(v->x());
          verts[vi * 3 + 1] = static_cast<float>(v->y());
          verts[vi * 3 + 2] = static_cast<float>(v->z());
          idx[vi] = vi;
          ++vi;
        }
      }

      rtcCommitGeometry(geom);
      rtcAttachGeometry(scene_, geom);
      rtcReleaseGeometry(geom);  // the scene keeps a reference
    }

    rtcCommitScene(scene_);
  }

  Hit closestHit(const Ray& ray) const override {
    RTCRayHit rh = {};
    rh.ray.org_x = static_cast<float>(ray.origin.x());
    rh.ray.org_y = static_cast<float>(ray.origin.y());
    rh.ray.org_z = static_cast<float>(ray.origin.z());
    rh.ray.dir_x = static_cast<float>(ray.direction.x());
    rh.ray.dir_y = static_cast<float>(ray.direction.y());
    rh.ray.dir_z = static_cast<float>(ray.direction.z());
    rh.ray.tnear = static_cast<float>(ray.tMin);
    rh.ray.tfar = clampTFar(ray.tMax);
    rh.ray.mask = 0xFFFFFFFFu;
    rh.ray.flags = 0;
    rh.ray.time = 0.0f;
    rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;

    rtcIntersect1(scene_, &rh);

    if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID) return Hit{};
    return Hit{true, static_cast<double>(rh.ray.tfar),
               static_cast<double>(rh.hit.u), static_cast<double>(rh.hit.v),
               static_cast<int>(rh.hit.primID)};
  }

  bool occluded(const Ray& ray) const override {
    RTCRay r = {};
    r.org_x = static_cast<float>(ray.origin.x());
    r.org_y = static_cast<float>(ray.origin.y());
    r.org_z = static_cast<float>(ray.origin.z());
    r.dir_x = static_cast<float>(ray.direction.x());
    r.dir_y = static_cast<float>(ray.direction.y());
    r.dir_z = static_cast<float>(ray.direction.z());
    r.tnear = static_cast<float>(ray.tMin);
    r.tfar = clampTFar(ray.tMax);
    r.mask = 0xFFFFFFFFu;
    r.flags = 0;
    r.time = 0.0f;

    rtcOccluded1(scene_, &r);
    // Embree sets tfar = -inf when the segment is blocked.
    return r.tfar < 0.0f;
  }

  Backend kind() const override { return Backend::Embree; }

 private:
  void releaseScene() {
    if (scene_) {
      rtcReleaseScene(scene_);
      scene_ = nullptr;
    }
    triangleCount_ = 0;
  }

  RTCDevice device_ = nullptr;
  RTCScene scene_ = nullptr;
  std::size_t triangleCount_ = 0;
};

}  // namespace

bool embreeDeviceAvailable() {
  RTCDevice d = rtcNewDevice(nullptr);
  if (!d) return false;
  rtcReleaseDevice(d);
  return true;
}

std::unique_ptr<IBackend> makeEmbreeBackend() {
  return std::make_unique<EmbreeBackend>();
}

}  // namespace rftrace
