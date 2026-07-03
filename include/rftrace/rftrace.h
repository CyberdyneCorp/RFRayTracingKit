/*
 * rftrace/rftrace.h — Stable C ABI over the RFTraceKit C++ core.
 *
 * This header is C-only (no C++ constructs): it uses opaque handle typedefs,
 * POD structs of plain double/int, a status enum, and function declarations
 * guarded by `extern "C"`. It is safe to include from a C translation unit and
 * from Swift's module map. The implementation (bindings/c/rftrace_c.cpp) wraps
 * the C++ core; no exception ever crosses this boundary.
 *
 * Ownership: every `*_create`/`*_run*` output is owned by the caller and must be
 * released with the matching `*_destroy`/`*_free`. All destroy/free calls are
 * NULL-safe no-ops. Variable-length results use the count-then-fill pattern:
 * query the count, allocate, then fill (RFTRACE_TRUNCATED if the buffer is
 * short — a partial copy is written, never an overflow).
 *
 * ABI growth is append-only: new `rftrace_settings` fields are added at the end;
 * callers memset(0) then call rftrace_settings_default() so old code keeps
 * working against a newer struct layout.
 */
#ifndef RFTRACE_RFTRACE_H
#define RFTRACE_RFTRACE_H

#include <stddef.h>
#include <stdint.h>

#define RFTRACE_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/* --- Opaque handles (never dereferenced by the caller) --------------------- */
typedef struct rftrace_scene        rftrace_scene;
typedef struct rftrace_simulator    rftrace_simulator;
typedef struct rftrace_result       rftrace_result;        /* wraps RFResult */
typedef struct rftrace_coverage     rftrace_coverage;      /* wraps CoverageResult */
typedef struct rftrace_route_result rftrace_route_result;  /* wraps RouteResult */

/* --- Status codes ---------------------------------------------------------- */
typedef enum {
  RFTRACE_OK = 0,
  RFTRACE_ERROR = 1,             /* caught std::exception / unknown throw */
  RFTRACE_INVALID_ARGUMENT = 2,  /* null handle, bad size, index out of range */
  RFTRACE_TRUNCATED = 3          /* count-then-fill buffer too small (partial) */
} rftrace_status;

/* --- Backend selector (mirrors rftrace::Backend order) --------------------- */
#define RFTRACE_BACKEND_CPU    0
#define RFTRACE_BACKEND_EMBREE 1
#define RFTRACE_BACKEND_METAL  2
#define RFTRACE_BACKEND_CUDA   3
#define RFTRACE_BACKEND_OPENCL 4

/* --- Propagation mode (mirrors rftrace::PropagationMode order) ------------- */
#define RFTRACE_MODE_IMAGE     0
#define RFTRACE_MODE_RAYLAUNCH 1

/* --- Diffraction model (mirrors rftrace::DiffractionModel order) ----------- */
#define RFTRACE_DIFFRACTION_SINGLE_EDGE 0
#define RFTRACE_DIFFRACTION_BULLINGTON  1
#define RFTRACE_DIFFRACTION_DEYGOUT     2
#define RFTRACE_DIFFRACTION_UTD         3

/* --- Small POD structs ----------------------------------------------------- */
typedef struct {
  double x, y, z;
} rftrace_vec3;

/* Mirrors rftrace::Material. `name` is borrowed for the duration of the call. */
typedef struct {
  const char* name;              /* NULL/"" => "default" */
  double relative_permittivity;  /* default 1.0 */
  double conductivity;           /* S/m */
  double roughness;              /* m */
  double penetration_loss_db;
  double reflection_loss_db;
} rftrace_material;

/*
 * POD mirror of rftrace::SimulationSettings (core + Phase-7 additive flags).
 * Fill with rftrace_settings_default() then override fields. New fields are
 * APPEND-ONLY: added at the end so a memset(0)+_default() caller stays
 * source-compatible across ABI growth.
 */
typedef struct {
  int      backend;              /* RFTRACE_BACKEND_* */
  int      mode;                 /* RFTRACE_MODE_* */
  int      max_reflections;      /* default 1 (0 = LOS only) */
  int      rays_per_transmitter; /* default 100000 (RayLaunch) */
  double   capture_radius;       /* default 2.0 */
  double   power_floor_dbm;      /* default -160.0 */
  uint64_t seed;                 /* default 1 */
  int      coherent;             /* 0/1, default 0 */
  int      allow_backend_fallback; /* 0/1, default 1 */
  int      thread_count;         /* default 0 (auto) */
  char     simulation_id[64];    /* default "rftrace_sim"; fixed buffer keeps POD */

  /* Phase-7 additive flags (default-off => archived behavior bit-for-bit). */
  int      enable_diffraction;
  int      diffraction_model;    /* RFTRACE_DIFFRACTION_* */
  int      enable_depolarization;
  int      enable_rain;
  double   rain_rate_mm_per_hr;
  int      enable_gaseous_attenuation;
  int      enable_vegetation;
  int      enable_sinr;
  double   noise_bandwidth_hz;   /* default 20e6 */
  double   noise_figure_db;      /* default 7.0 */
  double   noise_floor_dbm_override; /* NaN => derive from kTB+NF */
  /* APPEND-ONLY: new fields go here; bump nothing but keep _default() in sync. */
} rftrace_settings;

/* POD mirror of rftrace::CoverageGrid (flat evaluation height, v1). */
typedef struct {
  rftrace_vec3 origin;
  double cell_size;              /* default 2.0 */
  int    cols;                   /* >= 1 */
  int    rows;                   /* >= 1 */
  double height;                 /* default 1.5 */
} rftrace_grid;

/* --- Version / diagnostics ------------------------------------------------- */
/* Library semantic version string, e.g. "0.1.0". Never NULL. */
const char* rftrace_version(void);
/* Numeric ABI version (== RFTRACE_ABI_VERSION at build time). */
int rftrace_abi_version(void);
/* Thread-local last-error message; "" after a successful call. Never NULL. */
const char* rftrace_last_error(void);
/* Fill `s` with SimulationSettings{} defaults. */
void rftrace_settings_default(rftrace_settings* s);

/* --- Scene ----------------------------------------------------------------- */
rftrace_scene* rftrace_scene_create(void);
void           rftrace_scene_destroy(rftrace_scene* s);

/* Add (or replace by name) a material; writes its index to *out_index (may be NULL). */
rftrace_status rftrace_scene_add_material(rftrace_scene* s,
                                          const rftrace_material* m,
                                          int* out_index);
/* Append a triangle mesh from a flat vertex array of triangle_count*9 doubles
 * laid out [v0x v0y v0z v1x v1y v1z v2x v2y v2z ...]. `material_name` selects a
 * material by name; NULL/"" uses the default material. */
rftrace_status rftrace_scene_add_mesh(rftrace_scene* s, const double* vertices,
                                      size_t triangle_count,
                                      const char* material_name);
/* Float variant of rftrace_scene_add_mesh (triangle_count*9 floats). */
rftrace_status rftrace_scene_add_mesh_f(rftrace_scene* s, const float* vertices,
                                        size_t triangle_count,
                                        const char* material_name);
/* Append a mesh assigned to a material index (-1 => default material). */
rftrace_status rftrace_scene_add_mesh_indexed(rftrace_scene* s,
                                              const double* vertices,
                                              size_t triangle_count,
                                              int material_index);
rftrace_status rftrace_scene_add_transmitter(rftrace_scene* s, const char* id,
                                             rftrace_vec3 position,
                                             double frequency_hz,
                                             double power_dbm);
rftrace_status rftrace_scene_add_receiver(rftrace_scene* s, const char* id,
                                          rftrace_vec3 position);

/* --- Simulator ------------------------------------------------------------- */
rftrace_simulator* rftrace_simulator_create(const rftrace_settings* s);
void               rftrace_simulator_destroy(rftrace_simulator* sim);

/* run/coverage/route write a caller-owned result handle to *out only on OK. */
rftrace_status rftrace_simulator_run(rftrace_simulator* sim,
                                     const rftrace_scene* sc,
                                     rftrace_result** out);
rftrace_status rftrace_simulator_run_coverage(rftrace_simulator* sim,
                                              const rftrace_scene* sc,
                                              const rftrace_grid* g,
                                              rftrace_coverage** out);
rftrace_status rftrace_simulator_run_route(rftrace_simulator* sim,
                                           const rftrace_scene* sc,
                                           const rftrace_vec3* waypoints,
                                           size_t waypoint_count,
                                           double sample_spacing,
                                           double speed_mps,
                                           rftrace_route_result** out);

/* --- RFResult reading ------------------------------------------------------ */
void           rftrace_result_free(rftrace_result* r);
rftrace_status rftrace_result_frequency_hz(const rftrace_result* r, double* out);
rftrace_status rftrace_result_receiver_count(const rftrace_result* r, size_t* out);
/* Count-then-fill scalar arrays (indexed by receiver order). */
rftrace_status rftrace_result_receiver_powers(const rftrace_result* r,
                                              double* buf, size_t cap,
                                              size_t* written);
rftrace_status rftrace_result_receiver_path_loss(const rftrace_result* r,
                                                 double* buf, size_t cap,
                                                 size_t* written);
/* Receiver id string; needs cap >= len+1 for the NUL (else RFTRACE_TRUNCATED). */
rftrace_status rftrace_result_receiver_id(const rftrace_result* r, size_t i,
                                          char* buf, size_t cap, size_t* written);
rftrace_status rftrace_result_receiver_position(const rftrace_result* r,
                                                size_t i, rftrace_vec3* out);
rftrace_status rftrace_result_receiver_has_signal(const rftrace_result* r,
                                                  size_t i, int* out);
rftrace_status rftrace_result_receiver_received_power_dbm(const rftrace_result* r,
                                                          size_t i, double* out);
rftrace_status rftrace_result_receiver_path_loss_db(const rftrace_result* r,
                                                    size_t i, double* out);
rftrace_status rftrace_result_receiver_delay_spread_ns(const rftrace_result* r,
                                                       size_t i, double* out);
/* SINR (dB); NaN when settings.enable_sinr was off. */
rftrace_status rftrace_result_receiver_sinr_db(const rftrace_result* r, size_t i,
                                               double* out);

/* --- CoverageResult reading (row-major, size rows*cols) -------------------- */
void           rftrace_coverage_free(rftrace_coverage* c);
rftrace_status rftrace_coverage_dimensions(const rftrace_coverage* c, int* cols,
                                           int* rows);
rftrace_status rftrace_coverage_cell_count(const rftrace_coverage* c, size_t* out);
rftrace_status rftrace_coverage_power_dbm(const rftrace_coverage* c, double* buf,
                                          size_t cap, size_t* written);
rftrace_status rftrace_coverage_path_loss_db(const rftrace_coverage* c,
                                             double* buf, size_t cap,
                                             size_t* written);

/* --- RouteResult reading --------------------------------------------------- */
void           rftrace_route_result_free(rftrace_route_result* r);
rftrace_status rftrace_route_result_sample_count(const rftrace_route_result* r,
                                                 size_t* out);
rftrace_status rftrace_route_result_powers(const rftrace_route_result* r,
                                           double* buf, size_t cap,
                                           size_t* written);
rftrace_status rftrace_route_result_distances(const rftrace_route_result* r,
                                              double* buf, size_t cap,
                                              size_t* written);
rftrace_status rftrace_route_result_sample_position(const rftrace_route_result* r,
                                                    size_t i, rftrace_vec3* out);
rftrace_status rftrace_route_result_sample_has_signal(const rftrace_route_result* r,
                                                      size_t i, int* out);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* RFTRACE_RFTRACE_H */
