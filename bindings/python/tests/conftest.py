"""Shared fixtures and helpers for the rftracekit binding tests."""
from __future__ import annotations

import math

import pytest

import rftracekit as rf

# Golden single-transmitter / single-receiver link used across the suite.
TX_ID = "tx"
RX_ID = "rx"
TX_POS = [0.0, 0.0, 30.0]
RX_POS = [100.0, 0.0, 1.5]
FREQ_HZ = 3.5e9
POWER_DBM = 43.0

# Speed of light used by the C++ core (see include/rftrace/math.hpp constants::c).
SPEED_OF_LIGHT = 299792458.0


def free_space_path_loss_db(distance_m: float, frequency_hz: float) -> float:
    """Mirror of rftrace::rf::freeSpacePathLossDb for parity checks."""
    if distance_m <= 0.0 or frequency_hz <= 0.0:
        return 0.0
    return (
        20.0 * math.log10(distance_m)
        + 20.0 * math.log10(frequency_hz)
        + 20.0 * math.log10(4.0 * math.pi / SPEED_OF_LIGHT)
    )


def basic_scene() -> "rf.Scene":
    """A clear line-of-sight scene with one transmitter and one receiver."""
    scene = rf.Scene()
    scene.add_transmitter(
        id=TX_ID, position=TX_POS, frequency_hz=FREQ_HZ, power_dbm=POWER_DBM
    )
    scene.add_receiver(id=RX_ID, position=RX_POS)
    return scene


def wall(scene: "rf.Scene", material: str = "concrete") -> None:
    """Add a large wall in the plane y=100 (x in [0,300], z in [0,50])."""
    a, b, c, d = [0, 100, 0], [300, 100, 0], [300, 100, 50], [0, 100, 50]
    scene.add_mesh([rf.Triangle(a, b, c), rf.Triangle(a, c, d)], material)


def blocked_scene() -> "rf.Scene":
    """Transmitter and receiver on opposite sides of an opaque wall."""
    scene = rf.Scene()
    scene.add_material(rf.materials.preset("concrete"))
    wall(scene, "concrete")
    scene.add_transmitter(
        id=TX_ID, position=[150, 50, 10], frequency_hz=FREQ_HZ, power_dbm=POWER_DBM
    )
    scene.add_receiver(id=RX_ID, position=[150, 150, 10])
    return scene


@pytest.fixture
def scene():
    return basic_scene()


@pytest.fixture
def image_result(scene):
    settings = rf.SimulationSettings(mode="image", max_reflections=1)
    return rf.Simulator(settings).run(scene)
