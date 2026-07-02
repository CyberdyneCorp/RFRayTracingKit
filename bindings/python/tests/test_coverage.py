"""Coverage sweeps: array shape, no-signal cells, and exporters."""
from __future__ import annotations

import numpy as np

import rftracekit as rf
from conftest import wall


def _coverage_scene():
    scene = rf.Scene()
    scene.add_transmitter(
        id="tx", position=[5, 5, 30], frequency_hz=3.5e9, power_dbm=43
    )
    grid = rf.make_grid(origin=[0, 0, 0], cell_size=1.0, cols=10, rows=8, height=1.5)
    return scene, grid


def test_coverage_array_shape_and_matches_power_at():
    scene, grid = _coverage_scene()
    cov = rf.Simulator(rf.SimulationSettings(mode="image")).run_coverage(scene, grid)

    arr = cov.coverage_array
    assert arr.shape == (8, 10)
    assert arr.dtype == np.float64
    for row in range(8):
        for col in range(10):
            assert arr[row, col] == cov.power_at(row, col)
    assert cov.cell_centers().shape == (8, 10, 3)


def test_no_signal_cells_are_non_finite():
    # A wall at y=100 blocks every grid cell placed behind it.
    scene = rf.Scene()
    scene.add_material(rf.materials.preset("concrete"))
    wall(scene, "concrete")
    scene.add_transmitter(
        id="tx", position=[150, 50, 10], frequency_hz=3.5e9, power_dbm=43
    )
    grid = rf.make_grid(origin=[140, 140, 0], cell_size=1.0, cols=3, rows=3, height=10)
    cov = rf.Simulator(rf.SimulationSettings(mode="image", max_reflections=0)).run_coverage(
        scene, grid
    )

    arr = cov.coverage_array
    assert not np.any(np.isfinite(arr))  # every blocked cell is NaN/-inf
    assert cov.no_signal == float("-inf")


def test_coverage_exports_json_and_csv(tmp_path):
    scene, grid = _coverage_scene()
    cov = rf.Simulator(rf.SimulationSettings(mode="image")).run_coverage(scene, grid)

    assert cov.to_json().startswith("{")
    assert cov.to_geojson().startswith("{")
    assert cov.to_csv().strip() != ""

    json_path = tmp_path / "coverage.json"
    csv_path = tmp_path / "coverage.csv"
    cov.to_json(str(json_path))
    cov.to_csv(str(csv_path))
    assert json_path.exists() and json_path.stat().st_size > 0
    assert csv_path.exists() and csv_path.stat().st_size > 0
