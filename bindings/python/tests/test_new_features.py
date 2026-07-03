"""Python surface for the newer engine features: deterministic threadCount
parallelism, the geometry-driven UTD diffraction model, depolarization, and
backend selection/fallback. These settings were added to the C++ core after the
initial bindings; this locks in that they are reachable — and behave — from
Python."""
from __future__ import annotations

import numpy as np

import rftracekit as rf
from conftest import basic_scene, blocked_scene


def _coverage_scene() -> "rf.Scene":
    """A wall plus a transmitter, so a coverage grid has both lit and shadowed
    cells (exercises the parallelized per-cell work)."""
    scene = rf.Scene()
    scene.add_material(rf.materials.preset("concrete"))
    a, b, c, d = [40, 60, 0], [40, 60, 40], [40, 140, 40], [40, 140, 0]
    scene.add_mesh([rf.Triangle(a, b, c), rf.Triangle(a, c, d)], "concrete")
    scene.add_transmitter(
        id="tx", position=[10, 100, 30], frequency_hz=3.5e9, power_dbm=43
    )
    return scene


def test_thread_count_results_are_deterministic():
    """thread_count = 1 (serial) and 0 (all cores) must produce bit-identical
    coverage — the parallelism is over disjoint cells, so schedule-independent."""
    scene = _coverage_scene()
    grid = rf.make_grid(origin=(0, 40, 0), cell_size=8.0, cols=16, rows=16, height=1.5)

    serial = rf.Simulator(
        rf.SimulationSettings(thread_count=1, max_reflections=1)
    ).run_coverage(scene, grid)
    parallel = rf.Simulator(
        rf.SimulationSettings(thread_count=0, max_reflections=1)
    ).run_coverage(scene, grid)

    a = np.asarray(serial.coverage_array)
    b = np.asarray(parallel.coverage_array)
    assert a.shape == (16, 16)
    # Bit-for-bit (NaN in the same no-signal cells).
    np.testing.assert_array_equal(np.nan_to_num(a, nan=-1e30), np.nan_to_num(b, nan=-1e30))


def test_diffraction_model_selectable_by_string_and_enum():
    for name, expected in [
        ("single", rf.DiffractionModel.SingleEdge),
        ("bullington", rf.DiffractionModel.Bullington),
        ("deygout", rf.DiffractionModel.Deygout),
        ("utd", rf.DiffractionModel.UTD),
    ]:
        s = rf.SimulationSettings(enable_diffraction=True, diffraction_model=name)
        assert s.diffraction_model == expected
    # The enum value is accepted too.
    s = rf.SimulationSettings(diffraction_model=rf.DiffractionModel.UTD)
    assert s.diffraction_model == rf.DiffractionModel.UTD


def test_utd_diffraction_recovers_a_blocked_link():
    """With diffraction off a blocked receiver has no signal; enabling the UTD
    model gives it a finite diffracted path over the wall edge."""
    scene = blocked_scene()

    off = rf.Simulator(
        rf.SimulationSettings(max_reflections=0, enable_diffraction=False)
    ).run(scene)
    assert not off.receiver("rx").has_signal

    on = rf.Simulator(
        rf.SimulationSettings(
            max_reflections=0, enable_diffraction=True, diffraction_model="utd"
        )
    ).run(blocked_scene())
    rr = on.receiver("rx")
    assert rr.has_signal
    assert np.isfinite(rr.received_power_dbm)
    assert any(p.type == rf.PathType.Diffraction for p in rr.paths)


def test_enable_depolarization_is_settable_and_neutral_by_default():
    s = rf.SimulationSettings()
    assert s.enable_depolarization is False
    s.enable_depolarization = True
    assert s.enable_depolarization is True


def test_backend_selection_and_fallback():
    """A GPU/Embree backend is selected when available and otherwise falls back
    to CPU, so a run always succeeds regardless of the build/host."""
    assert rf.backend_available(rf.Backend.CPU)
    for name in ("cpu", "embree", "cuda", "opencl", "metal"):
        result = rf.Simulator(
            rf.SimulationSettings(backend=name, allow_backend_fallback=True,
                                  max_reflections=0)
        ).run(basic_scene())
        rr = result.receiver("rx")
        assert rr.has_signal and np.isfinite(rr.received_power_dbm)
