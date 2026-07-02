"""Optional visualization helpers.

Both plotting engines (pyvista, plotly) are optional and imported lazily, so
``import rftracekit`` (and even ``import rftracekit.viz``) always succeeds even
when neither is installed. Call :func:`plot_paths` / :func:`plot_coverage` and
select the backend with ``engine=``.
"""
from __future__ import annotations

import importlib


def _engine_module(engine: str):
    key = str(engine).strip().lower()
    if key not in {"pyvista", "plotly"}:
        raise ValueError(
            f"unknown viz engine {engine!r}; expected 'pyvista' or 'plotly'"
        )
    return importlib.import_module(f"{__name__}.{key}")


def plot_paths(result, engine: str = "pyvista", **kwargs):
    """Render transmitters, receivers and propagation paths in 3D."""
    return _engine_module(engine).plot_paths(result, **kwargs)


def plot_coverage(coverage, engine: str = "plotly", **kwargs):
    """Render a coverage heatmap."""
    return _engine_module(engine).plot_coverage(coverage, **kwargs)


__all__ = ["plot_paths", "plot_coverage"]
