"""Tests for the pure-Python rftracekit wrappers over the native extension."""
from __future__ import annotations

import importlib

import numpy as np
import pytest

import rftracekit as rf


def _basic_scene():
    scene = rf.Scene()
    scene.add_transmitter(
        id="tx", position=[0, 0, 30], frequency_hz=3.5e9, power_dbm=43
    )
    scene.add_receiver(id="rx", position=[100, 0, 1.5])
    return scene


def test_package_imports_without_optional_deps():
    # Importing the package and viz must never require pyvista/plotly/pandas.
    assert hasattr(rf, "Scene")
    assert importlib.import_module("rftracekit.viz") is rf.viz


def test_snake_case_scene_and_run():
    scene = _basic_scene()
    assert len(scene.transmitters()) == 1
    assert len(scene.receivers()) == 1

    result = rf.Simulator(rf.SimulationSettings()).run(scene)
    power = result.received_power_dbm
    assert isinstance(power, np.ndarray)
    assert power.shape == (1,)
    assert np.isfinite(power[0])


def test_settings_string_backend_and_mode():
    settings = rf.SimulationSettings(
        backend="cpu", mode="raylaunch", max_reflections=2, seed=7
    )
    assert settings.backend == rf.Backend.CPU
    assert settings.mode == rf.PropagationMode.RayLaunch
    assert settings.max_reflections == 2
    assert settings.seed == 7


def test_numpy_accessors_shapes():
    result = rf.Simulator(rf.SimulationSettings()).run(_basic_scene())
    assert result.receiver_positions.shape == (1, 3)
    assert result.received_power_dbm.shape == (1,)
    assert result.path_loss_db.shape == (1,)

    points, offsets = result.path_geometry()
    assert points.ndim == 2 and points.shape[1] == 3
    assert offsets.dtype == np.int32
    assert offsets[0] == 0
    assert offsets[-1] == points.shape[0]


def test_coverage_arrays_match_power_at():
    scene = rf.Scene()
    scene.add_transmitter(id="tx", position=[5, 5, 30], frequency_hz=3.5e9, power_dbm=43)
    grid = rf.make_grid(origin=[0, 0, 0], cell_size=1.0, cols=10, rows=8, height=1.5)
    cov = rf.Simulator(rf.SimulationSettings()).run_coverage(scene, grid)

    arr = cov.coverage_array
    assert arr.shape == (8, 10)
    for r in range(8):
        for c in range(10):
            assert arr[r, c] == pytest.approx(cov.power_at(r, c))
    assert cov.cell_centers().shape == (8, 10, 3)


def test_json_roundtrip(tmp_path):
    result = rf.Simulator(rf.SimulationSettings()).run(_basic_scene())
    text = result.to_json()
    assert isinstance(text, str) and "rx" in text

    path = tmp_path / "result.json"
    result.to_json(str(path))
    reloaded = rf.Result.from_json(str(path))
    assert reloaded.received_power_dbm[0] == pytest.approx(
        result.received_power_dbm[0]
    )


def test_exporters_produce_strings(tmp_path):
    result = rf.Simulator(rf.SimulationSettings()).run(_basic_scene())
    assert "receiver_id" in result.to_csv() or "rx" in result.to_csv()
    assert result.to_geojson(kind="receivers").startswith("{")
    assert result.to_geojson(kind="paths").startswith("{")
    assert result.to_gltf().strip() != ""

    # io module helpers accept both wrappers and native objects.
    assert rf.io.result_to_json_string(result) == rf.io.result_to_json_string(
        result.native
    )


def test_coverage_exporters(tmp_path):
    scene = rf.Scene()
    scene.add_transmitter(id="tx", position=[5, 5, 30], frequency_hz=3.5e9, power_dbm=43)
    grid = rf.make_grid(origin=[0, 0, 0], cell_size=1.0, cols=4, rows=4, height=1.5)
    cov = rf.Simulator(rf.SimulationSettings()).run_coverage(scene, grid)
    assert cov.to_json().startswith("{")
    assert cov.to_geojson().startswith("{")
    assert cov.to_csv().strip() != ""


def test_dataframes():
    pd = pytest.importorskip("pandas")
    result = rf.Simulator(rf.SimulationSettings()).run(_basic_scene())
    rdf = result.receivers_dataframe()
    assert isinstance(rdf, pd.DataFrame)
    assert "received_power_dbm" in rdf.columns
    assert len(rdf) == 1

    pdf = result.paths_dataframe()
    assert "type" in pdf.columns
    assert len(pdf) >= 1


def test_materials_and_antennas():
    mat = rf.materials.create(name="wall", relative_permittivity=5.0)
    assert mat.name == "wall"
    assert isinstance(rf.materials.has_preset("wall"), bool)
    ant = rf.antennas.omnidirectional(3.0)
    assert isinstance(ant, rf.AntennaPattern)


def test_missing_receiver_returns_none():
    result = rf.Simulator(rf.SimulationSettings()).run(_basic_scene())
    assert result.receiver("nope") is None
    assert result.receiver("rx") is not None
