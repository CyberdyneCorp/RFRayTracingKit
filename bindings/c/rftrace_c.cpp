// rftrace_c.cpp — implementation of the stable C ABI in include/rftrace/rftrace.h.
//
// Compiled as C++ into librftrace_c and linked against the core. Opaque C
// handles are plain heap pointers to the underlying C++ objects, cast with
// reinterpret_cast. Every fallible entry point runs its body inside RFTRACE_TRY
// so no C++ exception ever crosses into C (which would be UB); argument checks
// run before the try via RFTRACE_REQUIRE so bad input reports a distinct status.
// Variable-length results are copied out with the count-then-fill protocol, so
// no pointer into a C++ vector ever escapes a result's lifetime.

#include "rftrace/rftrace.h"

#include <cstring>
#include <string>
#include <vector>

#include "rftrace/coverage.hpp"
#include "rftrace/result.hpp"
#include "rftrace/route.hpp"
#include "rftrace/scene.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;

namespace {

// --- Last-error (thread-local) ----------------------------------------------
thread_local std::string g_last_error;

inline void set_last_error(const char* m) { g_last_error = m ? m : ""; }

// --- Handle <-> C++ casts ---------------------------------------------------
inline Scene* as_scene(rftrace_scene* h) { return reinterpret_cast<Scene*>(h); }
inline const Scene* as_scene(const rftrace_scene* h) {
  return reinterpret_cast<const Scene*>(h);
}
inline Simulator* as_sim(rftrace_simulator* h) {
  return reinterpret_cast<Simulator*>(h);
}
inline RFResult* as_result(rftrace_result* h) {
  return reinterpret_cast<RFResult*>(h);
}
inline const RFResult* as_result(const rftrace_result* h) {
  return reinterpret_cast<const RFResult*>(h);
}
inline const CoverageResult* as_coverage(const rftrace_coverage* h) {
  return reinterpret_cast<const CoverageResult*>(h);
}
inline const RouteResult* as_route(const rftrace_route_result* h) {
  return reinterpret_cast<const RouteResult*>(h);
}

// --- Count-then-fill of a value vector --------------------------------------
rftrace_status fill_doubles(const std::vector<double>& src, double* buf,
                            size_t cap, size_t* written) {
  const size_t n = src.size();
  const size_t w = n < cap ? n : cap;
  for (size_t i = 0; i < w; ++i) buf[i] = src[i];
  if (written) *written = w;
  return w < n ? RFTRACE_TRUNCATED : RFTRACE_OK;
}

// --- Enum mapping (C int -> C++ enum) ---------------------------------------
Backend backend_from_int(int b) {
  switch (b) {
    case RFTRACE_BACKEND_EMBREE: return Backend::Embree;
    case RFTRACE_BACKEND_METAL:  return Backend::Metal;
    case RFTRACE_BACKEND_CUDA:   return Backend::CUDA;
    case RFTRACE_BACKEND_OPENCL: return Backend::OpenCL;
    default:                     return Backend::CPU;
  }
}

PropagationMode mode_from_int(int m) {
  return m == RFTRACE_MODE_RAYLAUNCH ? PropagationMode::RayLaunch
                                     : PropagationMode::ImageMethod;
}

DiffractionModel diffraction_from_int(int d) {
  switch (d) {
    case RFTRACE_DIFFRACTION_BULLINGTON: return DiffractionModel::Bullington;
    case RFTRACE_DIFFRACTION_DEYGOUT:    return DiffractionModel::Deygout;
    case RFTRACE_DIFFRACTION_UTD:        return DiffractionModel::UTD;
    default:                             return DiffractionModel::SingleEdge;
  }
}

SimulationSettings to_settings(const rftrace_settings* s) {
  SimulationSettings out;
  out.backend = backend_from_int(s->backend);
  out.mode = mode_from_int(s->mode);
  out.maxReflections = s->max_reflections;
  out.raysPerTransmitter = s->rays_per_transmitter;
  out.captureRadius = s->capture_radius;
  out.powerFloorDbm = s->power_floor_dbm;
  out.seed = s->seed;
  out.coherent = s->coherent != 0;
  out.allowBackendFallback = s->allow_backend_fallback != 0;
  out.threadCount = s->thread_count;
  // Fixed char buffer -> std::string (NUL-terminated by construction below).
  char id[sizeof(s->simulation_id) + 1];
  std::memcpy(id, s->simulation_id, sizeof(s->simulation_id));
  id[sizeof(s->simulation_id)] = '\0';
  out.simulationId = id;
  out.enableDiffraction = s->enable_diffraction != 0;
  out.diffractionModel = diffraction_from_int(s->diffraction_model);
  out.enableDepolarization = s->enable_depolarization != 0;
  out.enableRain = s->enable_rain != 0;
  out.rainRateMmPerHr = s->rain_rate_mm_per_hr;
  out.enableGaseousAttenuation = s->enable_gaseous_attenuation != 0;
  out.enableVegetation = s->enable_vegetation != 0;
  out.enableSinr = s->enable_sinr != 0;
  out.noiseBandwidthHz = s->noise_bandwidth_hz;
  out.noiseFigureDb = s->noise_figure_db;
  out.noiseFloorDbmOverride = s->noise_floor_dbm_override;
  return out;
}

inline rftrace_vec3 to_c_vec3(const Vec3& p) {
  return rftrace_vec3{p.x(), p.y(), p.z()};
}

CoverageGrid to_grid(const rftrace_grid* g) {
  CoverageGrid grid;
  grid.origin = Vec3{g->origin.x, g->origin.y, g->origin.z};
  grid.cellSize = g->cell_size;
  grid.cols = g->cols;
  grid.rows = g->rows;
  grid.height = g->height;
  return grid;
}

std::vector<Triangle> to_triangles(const double* v, size_t triangle_count) {
  std::vector<Triangle> tris;
  tris.reserve(triangle_count);
  for (size_t t = 0; t < triangle_count; ++t) {
    const double* p = v + t * 9;
    tris.push_back(Triangle{Vec3{p[0], p[1], p[2]}, Vec3{p[3], p[4], p[5]},
                            Vec3{p[6], p[7], p[8]}});
  }
  return tris;
}

}  // namespace

// The no-throw boundary. Argument validation runs BEFORE the try so bad input
// is RFTRACE_INVALID_ARGUMENT, not RFTRACE_ERROR. The body runs inside an
// immediately-invoked lambda that the function `return`s, so every entry point
// ends on a return (no -Wreturn-type warning) and no exception escapes to C.
#define RFTRACE_TRY(...)                                                     \
  return [&]() -> rftrace_status {                                           \
    try {                                                                    \
      g_last_error.clear();                                                  \
      __VA_ARGS__;                                                           \
      return RFTRACE_OK;                                                     \
    } catch (const std::exception& e) {                                      \
      set_last_error(e.what());                                              \
      return RFTRACE_ERROR;                                                  \
    } catch (...) {                                                          \
      set_last_error("unknown error");                                       \
      return RFTRACE_ERROR;                                                  \
    }                                                                        \
  }()

#define RFTRACE_REQUIRE(cond, msg)                                           \
  do {                                                                       \
    if (!(cond)) {                                                           \
      set_last_error(msg);                                                   \
      return RFTRACE_INVALID_ARGUMENT;                                       \
    }                                                                        \
  } while (0)

extern "C" {

// --- Version / diagnostics --------------------------------------------------
const char* rftrace_version(void) { return "0.3.0"; }
int rftrace_abi_version(void) { return RFTRACE_ABI_VERSION; }
const char* rftrace_last_error(void) { return g_last_error.c_str(); }

void rftrace_settings_default(rftrace_settings* s) {
  if (!s) return;
  std::memset(s, 0, sizeof(*s));
  const SimulationSettings d;  // C++ defaults are the single source of truth.
  s->backend = RFTRACE_BACKEND_CPU;
  s->mode = RFTRACE_MODE_IMAGE;
  s->max_reflections = d.maxReflections;
  s->rays_per_transmitter = d.raysPerTransmitter;
  s->capture_radius = d.captureRadius;
  s->power_floor_dbm = d.powerFloorDbm;
  s->seed = d.seed;
  s->coherent = d.coherent ? 1 : 0;
  s->allow_backend_fallback = d.allowBackendFallback ? 1 : 0;
  s->thread_count = d.threadCount;
  std::memset(s->simulation_id, 0, sizeof(s->simulation_id));
  std::strncpy(s->simulation_id, d.simulationId.c_str(),
               sizeof(s->simulation_id) - 1);
  s->enable_diffraction = d.enableDiffraction ? 1 : 0;
  s->diffraction_model = RFTRACE_DIFFRACTION_SINGLE_EDGE;
  s->enable_depolarization = d.enableDepolarization ? 1 : 0;
  s->enable_rain = d.enableRain ? 1 : 0;
  s->rain_rate_mm_per_hr = d.rainRateMmPerHr;
  s->enable_gaseous_attenuation = d.enableGaseousAttenuation ? 1 : 0;
  s->enable_vegetation = d.enableVegetation ? 1 : 0;
  s->enable_sinr = d.enableSinr ? 1 : 0;
  s->noise_bandwidth_hz = d.noiseBandwidthHz;
  s->noise_figure_db = d.noiseFigureDb;
  s->noise_floor_dbm_override = d.noiseFloorDbmOverride;
}

// --- Scene ------------------------------------------------------------------
rftrace_scene* rftrace_scene_create(void) {
  try {
    return reinterpret_cast<rftrace_scene*>(new Scene());
  } catch (...) {
    set_last_error("scene allocation failed");
    return nullptr;
  }
}

void rftrace_scene_destroy(rftrace_scene* s) { delete as_scene(s); }

rftrace_status rftrace_scene_add_material(rftrace_scene* s,
                                          const rftrace_material* m,
                                          int* out_index) {
  RFTRACE_REQUIRE(s && m, "null argument");
  RFTRACE_TRY({
    Material mat;
    mat.name = (m->name && m->name[0]) ? m->name : "default";
    mat.relativePermittivity = m->relative_permittivity;
    mat.conductivity = m->conductivity;
    mat.roughness = m->roughness;
    mat.penetrationLossDb = m->penetration_loss_db;
    mat.reflectionLossDb = m->reflection_loss_db;
    const int idx = as_scene(s)->addMaterial(mat);
    if (out_index) *out_index = idx;
  });
}

rftrace_status rftrace_scene_add_mesh(rftrace_scene* s, const double* vertices,
                                      size_t triangle_count,
                                      const char* material_name) {
  RFTRACE_REQUIRE(s && (vertices || triangle_count == 0), "null argument");
  RFTRACE_TRY({
    as_scene(s)->addMesh(to_triangles(vertices, triangle_count),
                         material_name ? std::string(material_name)
                                       : std::string());
  });
}

rftrace_status rftrace_scene_add_mesh_f(rftrace_scene* s, const float* vertices,
                                        size_t triangle_count,
                                        const char* material_name) {
  RFTRACE_REQUIRE(s && (vertices || triangle_count == 0), "null argument");
  RFTRACE_TRY({
    std::vector<Triangle> tris;
    tris.reserve(triangle_count);
    for (size_t t = 0; t < triangle_count; ++t) {
      const float* p = vertices + t * 9;
      tris.push_back(Triangle{Vec3{p[0], p[1], p[2]}, Vec3{p[3], p[4], p[5]},
                              Vec3{p[6], p[7], p[8]}});
    }
    as_scene(s)->addMesh(tris, material_name ? std::string(material_name)
                                             : std::string());
  });
}

rftrace_status rftrace_scene_add_mesh_indexed(rftrace_scene* s,
                                              const double* vertices,
                                              size_t triangle_count,
                                              int material_index) {
  RFTRACE_REQUIRE(s && (vertices || triangle_count == 0), "null argument");
  RFTRACE_TRY({
    as_scene(s)->addMesh(to_triangles(vertices, triangle_count),
                         material_index);
  });
}

rftrace_status rftrace_scene_add_transmitter(rftrace_scene* s, const char* id,
                                             rftrace_vec3 position,
                                             double frequency_hz,
                                             double power_dbm) {
  RFTRACE_REQUIRE(s && id, "null argument");
  RFTRACE_TRY({
    Transmitter tx;
    tx.id = id;
    tx.position = Vec3{position.x, position.y, position.z};
    tx.frequencyHz = frequency_hz;
    tx.powerDbm = power_dbm;
    as_scene(s)->addTransmitter(tx);
  });
}

rftrace_status rftrace_scene_add_receiver(rftrace_scene* s, const char* id,
                                          rftrace_vec3 position) {
  RFTRACE_REQUIRE(s && id, "null argument");
  RFTRACE_TRY({
    Receiver rx;
    rx.id = id;
    rx.position = Vec3{position.x, position.y, position.z};
    as_scene(s)->addReceiver(rx);
  });
}

// --- Simulator --------------------------------------------------------------
rftrace_simulator* rftrace_simulator_create(const rftrace_settings* s) {
  if (!s) {
    set_last_error("null settings");
    return nullptr;
  }
  try {
    return reinterpret_cast<rftrace_simulator*>(new Simulator(to_settings(s)));
  } catch (const std::exception& e) {
    set_last_error(e.what());
    return nullptr;
  } catch (...) {
    set_last_error("simulator allocation failed");
    return nullptr;
  }
}

void rftrace_simulator_destroy(rftrace_simulator* sim) { delete as_sim(sim); }

rftrace_status rftrace_simulator_run(rftrace_simulator* sim,
                                     const rftrace_scene* sc,
                                     rftrace_result** out) {
  RFTRACE_REQUIRE(sim && sc && out, "null argument");
  RFTRACE_TRY({
    *out = reinterpret_cast<rftrace_result*>(
        new RFResult(as_sim(sim)->run(*as_scene(sc))));
  });
}

rftrace_status rftrace_simulator_run_coverage(rftrace_simulator* sim,
                                              const rftrace_scene* sc,
                                              const rftrace_grid* g,
                                              rftrace_coverage** out) {
  RFTRACE_REQUIRE(sim && sc && g && out, "null argument");
  RFTRACE_TRY({
    *out = reinterpret_cast<rftrace_coverage*>(new CoverageResult(
        as_sim(sim)->runCoverage(*as_scene(sc), to_grid(g))));
  });
}

rftrace_status rftrace_simulator_run_route(rftrace_simulator* sim,
                                           const rftrace_scene* sc,
                                           const rftrace_vec3* waypoints,
                                           size_t waypoint_count,
                                           double sample_spacing,
                                           double speed_mps,
                                           rftrace_route_result** out) {
  RFTRACE_REQUIRE(sim && sc && out && (waypoints || waypoint_count == 0),
                  "null argument");
  RFTRACE_TRY({
    Route route;
    route.sampleSpacing = sample_spacing;
    route.speedMps = speed_mps;
    route.waypoints.reserve(waypoint_count);
    for (size_t i = 0; i < waypoint_count; ++i)
      route.waypoints.push_back(
          Vec3{waypoints[i].x, waypoints[i].y, waypoints[i].z});
    *out = reinterpret_cast<rftrace_route_result*>(
        new RouteResult(as_sim(sim)->runRoute(*as_scene(sc), route)));
  });
}

// --- RFResult reading -------------------------------------------------------
void rftrace_result_free(rftrace_result* r) { delete as_result(r); }

rftrace_status rftrace_result_frequency_hz(const rftrace_result* r, double* out) {
  RFTRACE_REQUIRE(r && out, "null argument");
  RFTRACE_TRY({ *out = as_result(r)->frequencyHz; });
}

rftrace_status rftrace_result_receiver_count(const rftrace_result* r,
                                             size_t* out) {
  RFTRACE_REQUIRE(r && out, "null argument");
  RFTRACE_TRY({ *out = as_result(r)->receivers.size(); });
}

rftrace_status rftrace_result_receiver_powers(const rftrace_result* r,
                                              double* buf, size_t cap,
                                              size_t* written) {
  RFTRACE_REQUIRE(r && (buf || cap == 0), "null argument");
  RFTRACE_TRY({
    std::vector<double> v;
    for (const auto& rx : as_result(r)->receivers)
      v.push_back(rx.receivedPowerDbm);
    return fill_doubles(v, buf, cap, written);
  });
}

rftrace_status rftrace_result_receiver_path_loss(const rftrace_result* r,
                                                 double* buf, size_t cap,
                                                 size_t* written) {
  RFTRACE_REQUIRE(r && (buf || cap == 0), "null argument");
  RFTRACE_TRY({
    std::vector<double> v;
    for (const auto& rx : as_result(r)->receivers) v.push_back(rx.pathLossDb);
    return fill_doubles(v, buf, cap, written);
  });
}

rftrace_status rftrace_result_receiver_id(const rftrace_result* r, size_t i,
                                          char* buf, size_t cap,
                                          size_t* written) {
  RFTRACE_REQUIRE(r && (buf || cap == 0), "null argument");
  RFTRACE_REQUIRE(i < as_result(r)->receivers.size(), "receiver index OOR");
  RFTRACE_TRY({
    const std::string& id = as_result(r)->receivers[i].receiverId;
    const size_t need = id.size() + 1;  // include NUL
    if (cap < need) {
      const size_t w = cap;  // copy what fits, always NUL-terminate if room
      if (w > 0) {
        std::memcpy(buf, id.data(), w - 1);
        buf[w - 1] = '\0';
      }
      if (written) *written = w;
      return RFTRACE_TRUNCATED;
    }
    std::memcpy(buf, id.c_str(), need);
    if (written) *written = need;
    return RFTRACE_OK;
  });
}

rftrace_status rftrace_result_receiver_position(const rftrace_result* r,
                                                size_t i, rftrace_vec3* out) {
  RFTRACE_REQUIRE(r && out, "null argument");
  RFTRACE_REQUIRE(i < as_result(r)->receivers.size(), "receiver index OOR");
  RFTRACE_TRY({ *out = to_c_vec3(as_result(r)->receivers[i].position); });
}

rftrace_status rftrace_result_receiver_has_signal(const rftrace_result* r,
                                                  size_t i, int* out) {
  RFTRACE_REQUIRE(r && out, "null argument");
  RFTRACE_REQUIRE(i < as_result(r)->receivers.size(), "receiver index OOR");
  RFTRACE_TRY({ *out = as_result(r)->receivers[i].hasSignal ? 1 : 0; });
}

rftrace_status rftrace_result_receiver_received_power_dbm(const rftrace_result* r,
                                                          size_t i, double* out) {
  RFTRACE_REQUIRE(r && out, "null argument");
  RFTRACE_REQUIRE(i < as_result(r)->receivers.size(), "receiver index OOR");
  RFTRACE_TRY({ *out = as_result(r)->receivers[i].receivedPowerDbm; });
}

rftrace_status rftrace_result_receiver_path_loss_db(const rftrace_result* r,
                                                    size_t i, double* out) {
  RFTRACE_REQUIRE(r && out, "null argument");
  RFTRACE_REQUIRE(i < as_result(r)->receivers.size(), "receiver index OOR");
  RFTRACE_TRY({ *out = as_result(r)->receivers[i].pathLossDb; });
}

rftrace_status rftrace_result_receiver_delay_spread_ns(const rftrace_result* r,
                                                       size_t i, double* out) {
  RFTRACE_REQUIRE(r && out, "null argument");
  RFTRACE_REQUIRE(i < as_result(r)->receivers.size(), "receiver index OOR");
  RFTRACE_TRY({ *out = as_result(r)->receivers[i].delaySpreadNs; });
}

rftrace_status rftrace_result_receiver_sinr_db(const rftrace_result* r, size_t i,
                                               double* out) {
  RFTRACE_REQUIRE(r && out, "null argument");
  RFTRACE_REQUIRE(i < as_result(r)->receivers.size(), "receiver index OOR");
  RFTRACE_TRY({ *out = as_result(r)->receivers[i].sinrDb; });
}

// --- CoverageResult reading -------------------------------------------------
void rftrace_coverage_free(rftrace_coverage* c) { delete as_coverage(c); }

rftrace_status rftrace_coverage_dimensions(const rftrace_coverage* c, int* cols,
                                           int* rows) {
  RFTRACE_REQUIRE(c && cols && rows, "null argument");
  RFTRACE_TRY({
    *cols = as_coverage(c)->grid.cols;
    *rows = as_coverage(c)->grid.rows;
  });
}

rftrace_status rftrace_coverage_cell_count(const rftrace_coverage* c,
                                           size_t* out) {
  RFTRACE_REQUIRE(c && out, "null argument");
  RFTRACE_TRY({ *out = as_coverage(c)->powerDbm.size(); });
}

rftrace_status rftrace_coverage_power_dbm(const rftrace_coverage* c, double* buf,
                                          size_t cap, size_t* written) {
  RFTRACE_REQUIRE(c && (buf || cap == 0), "null argument");
  RFTRACE_TRY({ return fill_doubles(as_coverage(c)->powerDbm, buf, cap, written); });
}

rftrace_status rftrace_coverage_path_loss_db(const rftrace_coverage* c,
                                             double* buf, size_t cap,
                                             size_t* written) {
  RFTRACE_REQUIRE(c && (buf || cap == 0), "null argument");
  RFTRACE_TRY({
    return fill_doubles(as_coverage(c)->pathLossDb, buf, cap, written);
  });
}

// --- RouteResult reading ----------------------------------------------------
void rftrace_route_result_free(rftrace_route_result* r) { delete as_route(r); }

rftrace_status rftrace_route_result_sample_count(const rftrace_route_result* r,
                                                 size_t* out) {
  RFTRACE_REQUIRE(r && out, "null argument");
  RFTRACE_TRY({ *out = as_route(r)->samples.size(); });
}

rftrace_status rftrace_route_result_powers(const rftrace_route_result* r,
                                           double* buf, size_t cap,
                                           size_t* written) {
  RFTRACE_REQUIRE(r && (buf || cap == 0), "null argument");
  RFTRACE_TRY({
    std::vector<double> v;
    for (const auto& s : as_route(r)->samples) v.push_back(s.receivedPowerDbm);
    return fill_doubles(v, buf, cap, written);
  });
}

rftrace_status rftrace_route_result_distances(const rftrace_route_result* r,
                                              double* buf, size_t cap,
                                              size_t* written) {
  RFTRACE_REQUIRE(r && (buf || cap == 0), "null argument");
  RFTRACE_TRY({
    std::vector<double> v;
    for (const auto& s : as_route(r)->samples) v.push_back(s.distanceMeters);
    return fill_doubles(v, buf, cap, written);
  });
}

rftrace_status rftrace_route_result_sample_position(const rftrace_route_result* r,
                                                    size_t i, rftrace_vec3* out) {
  RFTRACE_REQUIRE(r && out, "null argument");
  RFTRACE_REQUIRE(i < as_route(r)->samples.size(), "sample index OOR");
  RFTRACE_TRY({ *out = to_c_vec3(as_route(r)->samples[i].position); });
}

rftrace_status rftrace_route_result_sample_has_signal(const rftrace_route_result* r,
                                                      size_t i, int* out) {
  RFTRACE_REQUIRE(r && out, "null argument");
  RFTRACE_REQUIRE(i < as_route(r)->samples.size(), "sample index OOR");
  RFTRACE_TRY({ *out = as_route(r)->samples[i].hasSignal ? 1 : 0; });
}

}  // extern "C"
