"""Plotly-backed plotting. ``plotly`` is imported lazily inside functions."""
from __future__ import annotations

import numpy as np

from .._util import native_of, require


def _plotly_go():
    return require("plotly.graph_objects", "viz")


def plot_paths(result, **kwargs):
    """Return a Plotly ``Figure`` with transmitters, receivers and paths in 3D."""
    go = _plotly_go()
    native = native_of(result)
    traces = []

    tx = np.asarray([t.position for t in native.transmitters], dtype=float).reshape(-1, 3)
    if tx.shape[0]:
        traces.append(
            go.Scatter3d(
                x=tx[:, 0], y=tx[:, 1], z=tx[:, 2], mode="markers",
                marker=dict(size=6, color="red"), name="transmitters",
            )
        )
    rx = np.asarray([r.position for r in native.receivers], dtype=float).reshape(-1, 3)
    if rx.shape[0]:
        traces.append(
            go.Scatter3d(
                x=rx[:, 0], y=rx[:, 1], z=rx[:, 2], mode="markers",
                marker=dict(size=4, color="blue"), name="receivers",
            )
        )
    for rec in native.receivers:
        for path in rec.paths:
            pts = np.asarray(path.points, dtype=float).reshape(-1, 3)
            if pts.shape[0] >= 2:
                traces.append(
                    go.Scatter3d(
                        x=pts[:, 0], y=pts[:, 1], z=pts[:, 2], mode="lines",
                        line=dict(color="green"), showlegend=False,
                    )
                )

    fig = go.Figure(data=traces)
    fig.update_layout(scene_aspectmode="data", **kwargs)
    return fig


def plot_coverage(coverage, **kwargs):
    """Return a Plotly ``Figure`` heatmap of a coverage grid."""
    go = _plotly_go()
    from ..results import CoverageResult

    cov = coverage if isinstance(coverage, CoverageResult) else CoverageResult(coverage)
    values = cov.coverage_array
    fig = go.Figure(
        data=go.Heatmap(z=values, colorbar=dict(title="dBm"), colorscale="Viridis")
    )
    fig.update_layout(
        xaxis_title="col", yaxis_title="row", yaxis_autorange="reversed", **kwargs
    )
    return fig


__all__ = ["plot_paths", "plot_coverage"]
