"""Exporters write files, produce strings, and JSON round-trips power."""
from __future__ import annotations

import pytest

import rftracekit as rf
from conftest import basic_scene


def _result():
    return rf.Simulator(rf.SimulationSettings(mode="image", max_reflections=1)).run(
        basic_scene()
    )


def test_export_strings():
    r = _result()
    assert r.to_json().startswith("{")
    assert r.to_csv().strip() != ""
    assert r.to_geojson(kind="receivers").startswith("{")
    assert r.to_geojson(kind="paths").startswith("{")
    assert r.to_gltf().strip() != ""


def test_exporters_write_files(tmp_path):
    r = _result()
    files = {
        "result.json": r.to_json,
        "result.csv": r.to_csv,
        "result.geojson": r.to_geojson,
        "result.gltf": r.to_gltf,
    }
    for name, fn in files.items():
        path = tmp_path / name
        fn(str(path))
        assert path.exists() and path.stat().st_size > 0, name


def test_json_roundtrip_preserves_power(tmp_path):
    r = _result()
    path = tmp_path / "result.json"
    r.to_json(str(path))

    reloaded = rf.Result.from_json(str(path))
    assert reloaded.received_power_dbm[0] == pytest.approx(r.received_power_dbm[0])

    from_str = rf.Result.from_json_string(r.to_json())
    assert from_str.received_power_dbm[0] == pytest.approx(r.received_power_dbm[0])


def test_io_helpers_accept_wrapper_or_native():
    r = _result()
    assert rf.io.result_to_json_string(r) == rf.io.result_to_json_string(r.native)
