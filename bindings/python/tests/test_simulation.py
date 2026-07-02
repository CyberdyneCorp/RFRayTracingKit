"""Point-to-point simulation behaviour: LOS, blocking, ray-launch, multi-bounce."""
from __future__ import annotations

import numpy as np

import rftracekit as rf
from conftest import basic_scene, blocked_scene


def test_image_method_los_has_finite_power():
    settings = rf.SimulationSettings(mode="image", max_reflections=0)
    result = rf.Simulator(settings).run(basic_scene())

    rr = result.receiver("rx")
    assert rr is not None
    assert rr.has_signal
    assert np.isfinite(rr.received_power_dbm)
    # A clear scene with maxReflections=0 yields exactly the direct path.
    assert len(rr.paths) == 1
    assert rr.paths[0].type == rf.PathType.LOS


def test_blocked_receiver_reports_no_signal():
    settings = rf.SimulationSettings(mode="image", max_reflections=0)
    result = rf.Simulator(settings).run(blocked_scene())

    rr = result.receiver("rx")
    assert rr is not None
    assert not rr.has_signal
    assert len(rr.paths) == 0


def test_settings_accept_string_backend_and_mode():
    settings = rf.SimulationSettings(
        backend="cpu", mode="raylaunch", max_reflections=3, seed=11
    )
    assert settings.backend == rf.Backend.CPU
    assert settings.mode == rf.PropagationMode.RayLaunch
    assert settings.max_reflections == 3
    assert settings.seed == 11


def test_raylaunch_runs_and_returns_result():
    settings = rf.SimulationSettings(
        mode="raylaunch", rays_per_transmitter=2000, max_reflections=1, seed=1
    )
    result = rf.Simulator(settings).run(basic_scene())
    assert isinstance(result, rf.Result)
    assert result.received_power_dbm.shape == (1,)
    assert np.isfinite(result.received_power_dbm[0])


def test_raylaunch_is_reproducible_with_same_seed():
    def run():
        settings = rf.SimulationSettings(
            mode="raylaunch", rays_per_transmitter=3000, max_reflections=1, seed=42
        )
        return rf.Simulator(settings).run(basic_scene()).received_power_dbm

    a = run()
    b = run()
    np.testing.assert_array_equal(a, b)


def test_multi_bounce_settings_accepted():
    for reflections in (1, 2, 3):
        settings = rf.SimulationSettings(mode="image", max_reflections=reflections)
        assert settings.max_reflections == reflections
        result = rf.Simulator(settings).run(basic_scene())
        assert result.received_power_dbm.shape == (1,)


def test_missing_receiver_returns_none():
    result = rf.Simulator(rf.SimulationSettings()).run(basic_scene())
    assert result.receiver("does-not-exist") is None
    assert result.receiver("rx") is not None
