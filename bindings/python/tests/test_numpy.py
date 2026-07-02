"""numpy accessors: shapes, float64 dtypes, and agreement with native scalars."""
from __future__ import annotations

import numpy as np

import rftracekit as rf
from conftest import basic_scene


def _result():
    return rf.Simulator(rf.SimulationSettings(mode="image", max_reflections=1)).run(
        basic_scene()
    )


def test_receiver_positions_shape_and_dtype():
    r = _result()
    pos = r.receiver_positions
    assert pos.shape == (1, 3)
    assert pos.dtype == np.float64
    np.testing.assert_allclose(pos[0], [100.0, 0.0, 1.5])


def test_power_and_loss_shapes_and_dtypes():
    r = _result()
    assert r.received_power_dbm.shape == (1,)
    assert r.path_loss_db.shape == (1,)
    assert r.received_power_dbm.dtype == np.float64
    assert r.path_loss_db.dtype == np.float64


def test_numpy_values_match_native_scalars():
    r = _result()
    rr = r.receiver("rx")
    assert r.received_power_dbm[0] == np.float64(rr.received_power_dbm)
    assert r.path_loss_db[0] == np.float64(rr.path_loss_db)
    np.testing.assert_allclose(
        r.receiver_positions[0], np.asarray(rr.position, dtype=np.float64)
    )


def test_path_geometry_offsets_are_consistent():
    r = _result()
    points, offsets = r.path_geometry()
    assert points.ndim == 2 and points.shape[1] == 3
    assert points.dtype == np.float64
    assert offsets.dtype == np.int32
    assert offsets[0] == 0
    assert offsets[-1] == points.shape[0]
    # offsets are monotonically non-decreasing.
    assert np.all(np.diff(offsets) >= 0)


def test_path_points_and_offsets_properties():
    # Spec (§10.7) exposes these as named attributes, not just path_geometry().
    r = _result()
    assert np.array_equal(r.path_points, r.path_geometry()[0])
    assert np.array_equal(r.path_offsets, r.path_geometry()[1])
    assert r.path_points.dtype == np.float64
    assert r.path_offsets.dtype == np.int32
    assert r.path_offsets[0] == 0
    assert r.path_offsets[-1] == r.path_points.shape[0]
