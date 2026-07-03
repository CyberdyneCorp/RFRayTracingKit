# RFTraceKit C API (`librftrace_c`)

A stable, no-throw `extern "C"` ABI over the C++ core — the foundation for
bindings in any FFI-capable language (Swift, Rust, Go, C#, C). The header
(`include/rftrace/rftrace.h`) is C-only; the implementation (`rftrace_c.cpp`)
wraps the C++ `Scene` / `Simulator` / result types.

## Build

Enabled with `RFTRACE_ENABLE_C_API=ON` (default OFF, so it never affects the
default C++ build):

```sh
just c-api      # configure + build librftrace_c + run the C ABI test
# or:
cmake -S . -B build-capi -DRFTRACE_ENABLE_C_API=ON
cmake --build build-capi
ctest --test-dir build-capi -R c_api --output-on-failure
```

## Contract

- **No throw crosses the boundary.** Every fallible call returns an
  `rftrace_status` (`RFTRACE_OK` on success). On failure, `rftrace_last_error()`
  returns a thread-local message.
- **Ownership is explicit.** Each `*_create` / `*_run*` output is caller-owned and
  released with the matching `*_destroy` / `*_free` (all NULL-safe).
- **Variable-length results use count-then-fill.** Query the count, allocate, then
  fill; a short buffer yields `RFTRACE_TRUNCATED` with a partial copy, never an
  overflow. Result data is copied out — no pointers into freed C++ storage.
- **ABI growth is append-only.** New `rftrace_settings` fields are appended;
  `memset(0)` + `rftrace_settings_default()` keeps old callers source-compatible.
  `rftrace_abi_version()` reports the ABI version.

## Example

```c
#include "rftrace/rftrace.h"

rftrace_scene* scene = rftrace_scene_create();
rftrace_vec3 tx = {0, 0, 0}, rx = {100, 0, 0};
rftrace_scene_add_transmitter(scene, "tx", tx, 3.5e9, 43.0);
rftrace_scene_add_receiver(scene, "rx", rx);

rftrace_settings s;
rftrace_settings_default(&s);
s.max_reflections = 0;                 /* LOS only */

rftrace_simulator* sim = rftrace_simulator_create(&s);
rftrace_result* result = NULL;
if (rftrace_simulator_run(sim, scene, &result) == RFTRACE_OK) {
    double power = 0.0;
    rftrace_result_receiver_received_power_dbm(result, 0, &power);
    printf("rx power = %.2f dBm\n", power);
    rftrace_result_free(result);
}
rftrace_simulator_destroy(sim);
rftrace_scene_destroy(scene);
```

See `tests/c_api_test.c` for a full end-to-end example (including error paths and
result agreement with the C++ `Simulator`).
