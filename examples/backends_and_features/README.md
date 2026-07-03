# backends_and_features — new-feature showcase (Python)

A self-contained Python script demonstrating the engine features added after the
initial bindings, all reachable now from `rftracekit`:

1. **Acceleration backends** — runs the same coverage on `cpu` / `embree` /
   `cuda` / `opencl` / `metal` (whichever are built and present) and checks that
   each agrees with the CPU reference (float-vs-double, so agreement is within a
   small tolerance).
2. **Deterministic threading** — `SimulationSettings(thread_count=…)` parallelizes
   the CPU run over cores with **bit-for-bit identical** results (`thread_count=1`
   is serial, `0` uses all cores).
3. **Geometry-driven UTD diffraction** — a blocked link recovered by diffraction
   over a mesh edge, comparing `diffraction_model="single"` (ITU-R knife edge) and
   `"utd"` (the Kouyoumjian–Pathak wedge; a free edge is a half-plane so UTD
   reduces to knife-edge, while a building corner uses the real wedge angle `n`).

## Run

```bash
# Build the Python extension (add backend flags to include Embree/CUDA/OpenCL):
just py-build
# or with backends:
cmake -S . -B build-py -DRFTRACE_ENABLE_PYTHON=ON -DRFTRACE_ENABLE_EMBREE=ON \
      -DRFTRACE_ENABLE_CUDA=ON -DOptiX_INSTALL_DIR=/path/to/OptiX && cmake --build build-py --target _native

PYTHONPATH=bindings/python python examples/backends_and_features/backends_and_features.py
```

Only `numpy` + `rftracekit` are required (no network, no plotting). Backends that
aren't built or have no device are skipped; the CPU backend is always available
and is the reference every other backend is validated against.

## Example output

```
== Acceleration backends ==
  cpu         0.37 s   covered 12.5%   (reference)
  embree      0.36 s   agree 100.0%   power-close True
  cuda        2.17 s   agree  99.9%   power-close True
  opencl      2.92 s   agree  99.7%   power-close True

== Deterministic CPU parallelism (thread_count) ==
  serial (1 thread)   0.37 s
  all cores           0.04 s   (9.5x)
  results identical:  True

== Geometry-driven UTD diffraction ==
  diffraction off:             no signal  (blocked)
  knife-edge (ITU-R P.526):    -72.19 dBm
  UTD (geometry-driven wedge): -74.56 dBm
```

> Note: per-call GPU backends carry OptiX/kernel setup + host↔device transfer, so
> for a single small coverage run the CPU/Embree path is faster here; the GPU
> wins on very large ray workloads. See `examples/sim_benchmark` and the CUDA
> section of the top-level README for the full performance discussion.
