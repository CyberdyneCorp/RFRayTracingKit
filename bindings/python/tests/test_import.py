"""The package (and its viz subpackage) must import without optional deps."""
from __future__ import annotations

import importlib

import rftracekit as rf


def test_top_level_symbols_present():
    for name in (
        "Scene",
        "Simulator",
        "SimulationSettings",
        "Result",
        "CoverageResult",
        "make_grid",
        "materials",
        "antennas",
        "io",
        "viz",
    ):
        assert hasattr(rf, name), name


def test_viz_import_is_safe_without_backends():
    # Importing viz must never pull in pyvista/plotly.
    mod = importlib.import_module("rftracekit.viz")
    assert mod is rf.viz
    assert hasattr(mod, "plot_paths")
    assert hasattr(mod, "plot_coverage")


def test_native_module_reachable():
    assert rf._native is importlib.import_module("rftracekit._native")
