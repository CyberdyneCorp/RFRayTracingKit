"""PyVista-backed 3D plotting. ``pyvista`` is imported lazily inside functions."""
from __future__ import annotations

import numpy as np

from .._util import native_of, require


def _pyvista():
    return require("pyvista", "viz")


def plot_paths(result, show: bool = True, plotter=None, **kwargs):
    """Plot transmitters, receivers and paths as a PyVista scene.

    Returns the :class:`pyvista.Plotter`. Pass ``show=False`` to build the scene
    without opening a window (useful for tests / off-screen rendering).
    """
    pv = _pyvista()
    native = native_of(result)
    plotter = plotter or pv.Plotter()

    for tx in native.transmitters:
        plotter.add_mesh(
            pv.Sphere(radius=1.0, center=np.asarray(tx.position, dtype=float)),
            color="red",
        )
    for rec in native.receivers:
        plotter.add_mesh(
            pv.Sphere(radius=0.8, center=np.asarray(rec.position, dtype=float)),
            color="blue",
        )
        for path in rec.paths:
            pts = np.asarray(path.points, dtype=float).reshape(-1, 3)
            if pts.shape[0] >= 2:
                plotter.add_mesh(pv.lines_from_points(pts), color="green", **kwargs)

    if show:
        plotter.show()
    return plotter


def plot_coverage(coverage, show: bool = True, plotter=None, cmap: str = "viridis", **kwargs):
    """Plot a coverage grid as a PyVista structured surface."""
    pv = _pyvista()
    from ..results import CoverageResult

    cov = coverage if isinstance(coverage, CoverageResult) else CoverageResult(coverage)
    centers = cov.cell_centers()
    values = cov.coverage_array
    plotter = plotter or pv.Plotter()
    grid = pv.StructuredGrid()
    grid.points = centers.reshape(-1, 3)
    grid.dimensions = (centers.shape[1], centers.shape[0], 1)
    grid["power_dbm"] = values.reshape(-1)
    plotter.add_mesh(grid, scalars="power_dbm", cmap=cmap, **kwargs)
    if show:
        plotter.show()
    return plotter


__all__ = ["plot_paths", "plot_coverage"]
