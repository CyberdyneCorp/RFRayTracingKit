"""Visualization helpers are optional and lazily imported."""
from __future__ import annotations

import pytest

import rftracekit as rf
from conftest import basic_scene


def _result():
    return rf.Simulator(rf.SimulationSettings(mode="image", max_reflections=1)).run(
        basic_scene()
    )


def _coverage():
    scene = rf.Scene()
    scene.add_transmitter(id="tx", position=[5, 5, 30], frequency_hz=3.5e9, power_dbm=43)
    grid = rf.make_grid(origin=[0, 0, 0], cell_size=1.0, cols=6, rows=6, height=1.5)
    return rf.Simulator(rf.SimulationSettings(mode="image")).run_coverage(scene, grid)


def test_viz_module_imports_without_backends():
    # Never raises, regardless of pyvista/plotly availability.
    import rftracekit.viz as viz

    assert callable(viz.plot_paths)
    assert callable(viz.plot_coverage)


def test_unknown_engine_raises_value_error():
    with pytest.raises(ValueError):
        rf.viz.plot_paths(_result(), engine="nope")


def test_plot_paths_plotly():
    go = pytest.importorskip("plotly.graph_objects")
    fig = _result().plot_3d(engine="plotly")
    assert isinstance(fig, go.Figure)


def test_plot_coverage_plotly():
    go = pytest.importorskip("plotly.graph_objects")
    fig = _coverage().plot_coverage(engine="plotly")
    assert isinstance(fig, go.Figure)


def test_plot_paths_pyvista():
    pv = pytest.importorskip("pyvista")
    plotter = _result().plot_3d(engine="pyvista", show=False)
    assert isinstance(plotter, pv.Plotter)
