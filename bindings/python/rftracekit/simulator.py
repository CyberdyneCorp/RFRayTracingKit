"""Simulation settings, the simulator, and the coverage grid."""
from __future__ import annotations

from typing import Optional, Sequence

from . import _native
from ._util import native_of, parse_backend, parse_mode
from .results import CoverageResult, Result
from .route import RouteResult

CoverageGrid = _native.CoverageGrid
PropagationMode = _native.PropagationMode
Backend = _native.Backend


def SimulationSettings(
    backend="cpu",
    mode="image",
    max_reflections: Optional[int] = None,
    rays_per_transmitter: Optional[int] = None,
    capture_radius: Optional[float] = None,
    power_floor_dbm: Optional[float] = None,
    seed: Optional[int] = None,
    coherent: Optional[bool] = None,
    allow_backend_fallback: Optional[bool] = None,
    simulation_id: Optional[str] = None,
) -> "_native.SimulationSettings":
    """Build a native :class:`SimulationSettings` from friendly arguments.

    ``backend`` accepts ``'cpu'``, ``'metal'``, ``'embree'``, ``'cuda'`` or
    ``'opencl'`` (or a :class:`Backend`); ``mode`` accepts ``'image'`` or
    ``'raylaunch'`` (or a :class:`PropagationMode`). Unspecified fields keep
    their C++ defaults.
    """
    settings = _native.SimulationSettings(
        backend=parse_backend(backend), mode=parse_mode(mode)
    )
    _apply(settings, "max_reflections", max_reflections)
    _apply(settings, "rays_per_transmitter", rays_per_transmitter)
    _apply(settings, "capture_radius", capture_radius)
    _apply(settings, "power_floor_dbm", power_floor_dbm)
    _apply(settings, "seed", seed)
    _apply(settings, "coherent", coherent)
    _apply(settings, "allow_backend_fallback", allow_backend_fallback)
    _apply(settings, "simulation_id", simulation_id)
    return settings


def _apply(settings, name, value):
    if value is not None:
        setattr(settings, name, value)


def make_grid(
    origin: Sequence[float] = (0.0, 0.0, 0.0),
    cell_size: float = 2.0,
    cols: int = 1,
    rows: int = 1,
    height: float = 1.5,
) -> "CoverageGrid":
    """Keyword-friendly constructor for a :class:`CoverageGrid`."""
    return _native.CoverageGrid(
        origin=list(origin),
        cell_size=cell_size,
        cols=cols,
        rows=rows,
        height=height,
    )


class Simulator:
    """Runs propagation simulations against a :class:`Scene`."""

    def __init__(self, settings=None) -> None:
        if settings is None:
            settings = _native.SimulationSettings()
        self.settings = native_of(settings)
        self.native = _native.Simulator(self.settings)

    def run(self, scene) -> Result:
        """Run a point-to-point simulation and return a :class:`Result`."""
        return Result(self.native.run(native_of(scene)))

    def run_coverage(self, scene, grid) -> CoverageResult:
        """Run a coverage sweep over ``grid`` and return a :class:`CoverageResult`."""
        return CoverageResult(
            self.native.run_coverage(native_of(scene), native_of(grid))
        )

    def run_route(self, scene, route) -> RouteResult:
        """Simulate a drive-test ``route`` and return a :class:`RouteResult`.

        The route is sampled by arc length; each sample is evaluated as a point
        receiver and the ordered per-sample metrics (including ``doppler_hz``)
        are collected in route order.
        """
        return RouteResult(
            self.native.run_route(native_of(scene), native_of(route))
        )


__all__ = [
    "SimulationSettings",
    "Simulator",
    "CoverageGrid",
    "make_grid",
    "PropagationMode",
    "Backend",
    "RouteResult",
]
