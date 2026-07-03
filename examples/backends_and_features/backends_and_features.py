#!/usr/bin/env python3
"""Showcase of the newer engine features from Python:

  1. Acceleration backends  — run the same coverage on CPU / Embree / CUDA /
     OpenCL (whichever are built + present) and check they agree.
  2. Deterministic threading — `thread_count` parallelizes the CPU run over
     cores with bit-for-bit identical results.
  3. Geometry-driven UTD     — diffraction over a real building corner, compared
     to the ITU-R knife edge, recovering a shadowed link.

Self-contained (numpy + rftracekit only); no network or plotting. Run:

    just py-build
    PYTHONPATH=bindings/python python examples/backends_and_features/backends_and_features.py
"""
from __future__ import annotations

import time

import numpy as np

import rftracekit as rf


def box(scene, lo, hi, material):
    """Add the 12 triangles of an axis-aligned box [lo, hi]."""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    p = [
        [x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0],
        [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1],
    ]
    faces = [(0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6), (0, 4, 5), (0, 5, 1),
             (3, 2, 6), (3, 6, 7), (1, 5, 6), (1, 6, 2), (0, 3, 7), (0, 7, 4)]
    scene.add_mesh([rf.Triangle(p[a], p[b], p[c]) for a, b, c in faces], material)


def city_scene(side=6, pitch=40.0):
    """A grid of buildings with a transmitter above the centre."""
    scene = rf.Scene()
    scene.add_material(rf.materials.preset("concrete"))
    for i in range(side):
        for j in range(side):
            x, y = i * pitch, j * pitch
            box(scene, [x + 6, y + 6, 0], [x + pitch - 6, y + pitch - 6, 30], "concrete")
    span = side * pitch
    scene.add_transmitter(
        id="tx", position=[span / 2, span / 2, 60], frequency_hz=3.5e9, power_dbm=46
    )
    return scene, span


def timed_coverage(scene, grid, **settings):
    t0 = time.perf_counter()
    cov = rf.Simulator(rf.SimulationSettings(**settings)).run_coverage(scene, grid)
    return np.asarray(cov.coverage_array), time.perf_counter() - t0


def covered_fraction(a):
    return float(np.mean(np.isfinite(a)))


def agreement(a, b):
    finite = np.isfinite(a) & np.isfinite(b)
    same_mask = np.isfinite(a) == np.isfinite(b)
    close = np.allclose(a[finite], b[finite], atol=1.0)  # float32 vs double slack
    return float(np.mean(same_mask)), close


def main():
    print(f"RFTraceKit {getattr(rf, '__version__', '?')}  —  new-feature showcase\n")
    scene, span = city_scene()
    grid = rf.make_grid(
        origin=(0, 0, 0), cell_size=span / 120, cols=120, rows=120, height=1.5
    )
    n = grid.cols * grid.rows
    print(f"Scene: 36 buildings, {n} coverage cells, image method (1 reflection)\n")

    # 1) Backends -------------------------------------------------------------
    print("== Acceleration backends ==")
    ref, ref_t = timed_coverage(scene, grid, backend="cpu", thread_count=1,
                                max_reflections=1)
    print(f"  cpu        {ref_t:6.3f} s   covered {covered_fraction(ref) * 100:5.1f}%   (reference)")
    for name in ("embree", "cuda", "opencl", "metal"):
        if not rf.backend_available(getattr(rf.Backend, {"embree": "Embree",
                "cuda": "CUDA", "opencl": "OpenCL", "metal": "Metal"}[name])):
            print(f"  {name:<10} not available (skipped)")
            continue
        arr, t = timed_coverage(scene, grid, backend=name, thread_count=1,
                                max_reflections=1, allow_backend_fallback=False)
        agree, close = agreement(ref, arr)
        print(f"  {name:<10} {t:6.3f} s   agree {agree * 100:5.1f}%   power-close {close}")

    # 2) Deterministic threading ---------------------------------------------
    print("\n== Deterministic CPU parallelism (thread_count) ==")
    par, par_t = timed_coverage(scene, grid, backend="cpu", thread_count=0,
                                max_reflections=1)
    identical = np.array_equal(np.nan_to_num(ref, nan=-1e30),
                               np.nan_to_num(par, nan=-1e30))
    speedup = ref_t / par_t if par_t else 0.0
    print(f"  serial (1 thread)   {ref_t:6.3f} s")
    print(f"  all cores           {par_t:6.3f} s   ({speedup:.1f}x)")
    print(f"  results identical:  {identical}")

    # 3) Geometry-driven UTD diffraction -------------------------------------
    print("\n== Geometry-driven UTD diffraction ==")
    # A screen blocks the direct path; diffraction over its top edge recovers the
    # link. UTD extracts the real wedge angle from the mesh (this free edge is a
    # half-plane, n = 2, so UTD reduces to the ITU-R knife edge; a building corner
    # would give n = 1.5). tx/rx sit below the 40 m edge on opposite sides.
    d = rf.Scene()
    d.add_material(rf.materials.preset("concrete"))
    a, b, c, e = [0, -30, 0], [0, 30, 0], [0, 30, 40], [0, -30, 40]
    d.add_mesh([rf.Triangle(a, b, c), rf.Triangle(a, c, e)], "concrete")
    d.add_transmitter(id="tx", position=[-40, 0, 10], frequency_hz=3.5e9, power_dbm=43)
    d.add_receiver(id="rx", position=[40, 0, 10])

    def rx_power(**s):
        r = rf.Simulator(rf.SimulationSettings(max_reflections=0, **s)).run(d).receiver("rx")
        return r.received_power_dbm if r.has_signal else None

    def fmt(v):
        return f"{v:.2f} dBm" if v is not None else "no signal"

    print(f"  diffraction off:             {fmt(rx_power())}  (blocked)")
    print(f"  knife-edge (ITU-R P.526):    {fmt(rx_power(enable_diffraction=True, diffraction_model='single'))}")
    print(f"  UTD (geometry-driven wedge): {fmt(rx_power(enable_diffraction=True, diffraction_model='utd'))}")

    print("\nAll sections ran; the CPU backend is always available and is the reference.")


if __name__ == "__main__":
    main()
