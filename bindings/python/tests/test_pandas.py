"""pandas DataFrame views (pandas is an optional dependency)."""
from __future__ import annotations

import pytest

import rftracekit as rf
from conftest import basic_scene


def _result():
    return rf.Simulator(rf.SimulationSettings(mode="image", max_reflections=1)).run(
        basic_scene()
    )


def test_receivers_dataframe_columns():
    pd = pytest.importorskip("pandas")
    df = _result().receivers_dataframe()
    assert isinstance(df, pd.DataFrame)
    assert len(df) == 1
    for col in (
        "receiver_id",
        "x",
        "y",
        "z",
        "has_signal",
        "received_power_dbm",
        "path_loss_db",
        "num_paths",
    ):
        assert col in df.columns, col
    assert df.iloc[0]["receiver_id"] == "rx"


def test_paths_dataframe_columns():
    pd = pytest.importorskip("pandas")
    df = _result().paths_dataframe()
    assert isinstance(df, pd.DataFrame)
    assert len(df) >= 1
    for col in (
        "transmitter_id",
        "receiver_id",
        "type",
        "received_power_dbm",
        "path_loss_db",
        "num_points",
    ):
        assert col in df.columns, col
