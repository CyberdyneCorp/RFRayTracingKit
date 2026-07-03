/*
 * c_api_test.c — end-to-end test of the RFTraceKit C ABI, compiled AS C.
 *
 * Includes ONLY <rftrace/rftrace.h> and links librftrace_c. It proves the header
 * is C-compatible, exercises scene/simulator/results end to end, checks the
 * received power against the core's own free-space link budget (bit-for-bit),
 * and drives the error / truncation paths. Returns non-zero on the first failure.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rftrace/rftrace.h"

static int failures = 0;

#define CHECK(cond, msg)                                                   \
  do {                                                                     \
    if (!(cond)) {                                                         \
      fprintf(stderr, "FAIL: %s (line %d): %s\n", msg, __LINE__, #cond);   \
      ++failures;                                                          \
    }                                                                      \
  } while (0)

/* The core's free-space path loss (rf/free_space_path_loss.hpp), replicated in C
 * so the golden is derived from the same formula the C++ Simulator uses. */
static double fspl_db(double d, double f) {
  const double pi = 3.14159265358979323846;
  const double c = 299792458.0;
  return 20.0 * log10(d) + 20.0 * log10(f) + 20.0 * log10(4.0 * pi / c);
}

int main(void) {
  /* 1) VERSION / ABI. */
  CHECK(rftrace_abi_version() == RFTRACE_ABI_VERSION, "abi version matches macro");
  CHECK(rftrace_version() != NULL, "version string non-NULL");

  /* 2) DEFAULTS mirror SimulationSettings{}. */
  rftrace_settings s;
  rftrace_settings_default(&s);
  CHECK(s.backend == RFTRACE_BACKEND_CPU, "default backend CPU");
  CHECK(s.mode == RFTRACE_MODE_IMAGE, "default mode image method");
  CHECK(s.max_reflections == 1, "default max_reflections 1");
  CHECK(s.seed == 1, "default seed 1");
  CHECK(s.thread_count == 0, "default thread_count 0");
  CHECK(s.coherent == 0, "default coherent off");
  CHECK(strcmp(s.simulation_id, "rftrace_sim") == 0, "default simulation id");

  /* 3) END-TO-END VALUE MATCH: the UnobstructedLos scenario. */
  s.max_reflections = 0;  /* LOS only */

  rftrace_scene* scene = rftrace_scene_create();
  CHECK(scene != NULL, "scene created");

  rftrace_vec3 tx_pos = {0.0, 0.0, 10.0};
  rftrace_vec3 rx_pos = {100.0, 0.0, 10.0};
  CHECK(rftrace_scene_add_transmitter(scene, "tx", tx_pos, 3.5e9, 43.0) ==
            RFTRACE_OK,
        "add transmitter");
  CHECK(rftrace_scene_add_receiver(scene, "rx", rx_pos) == RFTRACE_OK,
        "add receiver");

  rftrace_simulator* sim = rftrace_simulator_create(&s);
  CHECK(sim != NULL, "simulator created");

  rftrace_result* res = NULL;
  CHECK(rftrace_simulator_run(sim, scene, &res) == RFTRACE_OK, "run");
  CHECK(res != NULL, "result handle produced");

  size_t rx_count = 0;
  CHECK(rftrace_result_receiver_count(res, &rx_count) == RFTRACE_OK,
        "receiver count query");
  CHECK(rx_count == 1, "one receiver");

  int has_signal = 0;
  CHECK(rftrace_result_receiver_has_signal(res, 0, &has_signal) == RFTRACE_OK,
        "has_signal query");
  CHECK(has_signal == 1, "receiver has signal");

  /* Golden: Ptx - FSPL, gains 0 (omni), co-polar => no polarization loss. */
  const double expected = 43.0 - fspl_db(100.0, 3.5e9);
  double got = 0.0;
  CHECK(rftrace_result_receiver_received_power_dbm(res, 0, &got) == RFTRACE_OK,
        "received power query");
  CHECK(fabs(got - expected) < 1e-9, "received power matches core link budget");

  /* Same value via the count-then-fill array accessor. */
  double pbuf[1] = {0.0};
  size_t written = 0;
  CHECK(rftrace_result_receiver_powers(res, pbuf, 1, &written) == RFTRACE_OK,
        "receiver powers fill (exact cap)");
  CHECK(written == 1, "one power written");
  CHECK(fabs(pbuf[0] - expected) < 1e-9, "array power matches scalar");

  /* Receiver id round-trips through the string accessor. */
  char idbuf[8];
  size_t idlen = 0;
  CHECK(rftrace_result_receiver_id(res, 0, idbuf, sizeof(idbuf), &idlen) ==
            RFTRACE_OK,
        "receiver id fill");
  CHECK(strcmp(idbuf, "rx") == 0, "receiver id value");

  /* 4) COVERAGE + ROUTE smoke. */
  rftrace_grid grid;
  grid.origin.x = 0.0;
  grid.origin.y = 0.0;
  grid.origin.z = 0.0;
  grid.cell_size = 10.0;
  grid.cols = 2;
  grid.rows = 2;
  grid.height = 1.5;

  rftrace_coverage* cov = NULL;
  CHECK(rftrace_simulator_run_coverage(sim, scene, &grid, &cov) == RFTRACE_OK,
        "run coverage");
  size_t cells = 0;
  CHECK(rftrace_coverage_cell_count(cov, &cells) == RFTRACE_OK, "cell count");
  CHECK(cells == 4, "2x2 grid has 4 cells");
  double cbuf[4] = {0};
  size_t cwritten = 0;
  CHECK(rftrace_coverage_power_dbm(cov, cbuf, 4, &cwritten) == RFTRACE_OK,
        "coverage power fill");
  CHECK(cwritten == 4, "four cells written");
  int cols = 0, rows = 0;
  CHECK(rftrace_coverage_dimensions(cov, &cols, &rows) == RFTRACE_OK, "dims");
  CHECK(cols == 2 && rows == 2, "coverage dims 2x2");

  rftrace_vec3 wps[2] = {{0.0, 0.0, 10.0}, {50.0, 0.0, 10.0}};
  rftrace_route_result* route = NULL;
  CHECK(rftrace_simulator_run_route(sim, scene, wps, 2, 10.0, 0.0, &route) ==
            RFTRACE_OK,
        "run route");
  size_t samples = 0;
  CHECK(rftrace_route_result_sample_count(route, &samples) == RFTRACE_OK,
        "route sample count");
  CHECK(samples >= 1, "route has samples");
  double rbuf[64];
  size_t rwritten = 0;
  CHECK(rftrace_route_result_powers(route, rbuf,
                                    samples < 64 ? samples : 64, &rwritten) ==
            RFTRACE_OK,
        "route powers fill");

  /* 5) ERROR PATHS. */
  CHECK(rftrace_simulator_run(NULL, scene, &res) == RFTRACE_INVALID_ARGUMENT,
        "null simulator -> invalid arg");
  CHECK(rftrace_last_error()[0] != '\0', "last error set for null arg");

  /* add_mesh referencing a non-existent material -> RFTRACE_ERROR (SceneError). */
  double tri[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  CHECK(rftrace_scene_add_mesh(scene, tri, 1, "no_such_material") ==
            RFTRACE_ERROR,
        "unknown material -> error");
  CHECK(rftrace_last_error()[0] != '\0', "last error set for scene error");

  /* Out-of-range receiver index -> invalid argument. */
  double dummy = 0.0;
  CHECK(rftrace_result_receiver_received_power_dbm(res, 99, &dummy) ==
            RFTRACE_INVALID_ARGUMENT,
        "receiver index OOR -> invalid arg");

  /* 6) SHORT-BUFFER TRUNCATION on the coverage array (count 4 > cap 2). */
  double small[3] = {1.0, 2.0, -12345.0};  /* index 2 is a canary */
  size_t tw = 0;
  CHECK(rftrace_coverage_power_dbm(cov, small, 2, &tw) == RFTRACE_TRUNCATED,
        "short buffer -> truncated");
  CHECK(tw == 2, "truncated write count == cap");
  CHECK(small[2] == -12345.0, "canary past cap untouched");

  /* Success clears the last error. */
  CHECK(rftrace_result_frequency_hz(res, &dummy) == RFTRACE_OK, "freq query ok");
  CHECK(rftrace_last_error()[0] == '\0', "last error cleared on success");

  /* 7) NO LEAKS: destroy everything. */
  rftrace_route_result_free(route);
  rftrace_coverage_free(cov);
  rftrace_result_free(res);
  rftrace_simulator_destroy(sim);
  rftrace_scene_destroy(scene);

  /* NULL-safe destroy/free are no-ops. */
  rftrace_scene_destroy(NULL);
  rftrace_simulator_destroy(NULL);
  rftrace_result_free(NULL);
  rftrace_coverage_free(NULL);
  rftrace_route_result_free(NULL);

  if (failures == 0) {
    printf("c_api_test: all checks passed\n");
    return 0;
  }
  fprintf(stderr, "c_api_test: %d check(s) failed\n", failures);
  return 1;
}
