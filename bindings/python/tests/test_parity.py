"""Parity with the C++ golden scenes (see tests/test_golden.cpp)."""
from __future__ import annotations

import math

import numpy as np
import pytest

import rftracekit as rf
from conftest import (
    FREQ_HZ,
    POWER_DBM,
    RX_POS,
    TX_POS,
    basic_scene,
    free_space_path_loss_db,
)


def test_empty_scene_los_matches_free_space_budget():
    # Golden.EmptySceneLos: 43 dBm - FSPL(dist, 3.5 GHz), maxReflections=2.
    settings = rf.SimulationSettings(mode="image", max_reflections=2)
    result = rf.Simulator(settings).run(basic_scene())

    rr = result.receiver("rx")
    assert rr is not None and rr.has_signal
    assert len(rr.paths) == 1  # no geometry, so LOS only

    dist = math.dist(TX_POS, RX_POS)
    expected = POWER_DBM - free_space_path_loss_db(dist, FREQ_HZ)
    assert rr.paths[0].received_power_dbm == pytest.approx(expected, abs=1e-6)
    assert result.received_power_dbm[0] == pytest.approx(expected, abs=1e-6)


def test_single_wall_reflection_is_weaker_than_los():
    # Golden.SingleWallReflection: LOS + one reflection off a concrete wall.
    scene = rf.Scene()
    scene.add_material(rf.materials.preset("concrete"))
    a, b, c, d = [0, 100, 0], [300, 100, 0], [300, 100, 50], [0, 100, 50]
    scene.add_mesh([rf.Triangle(a, b, c), rf.Triangle(a, c, d)], "concrete")
    scene.add_transmitter(id="tx", position=[100, 20, 20], frequency_hz=FREQ_HZ, power_dbm=POWER_DBM)
    scene.add_receiver(id="rx", position=[200, 20, 10])

    result = rf.Simulator(rf.SimulationSettings(mode="image", max_reflections=1)).run(scene)
    rr = result.receiver("rx")
    assert rr is not None
    assert len(rr.paths) == 2  # LOS + reflection

    los = next(p for p in rr.paths if p.type == rf.PathType.LOS)
    refl = next(p for p in rr.paths if p.type != rf.PathType.LOS)
    assert refl.received_power_dbm < los.received_power_dbm
    # Aggregate power is at least the LOS component alone.
    assert rr.received_power_dbm > los.received_power_dbm - 0.1
