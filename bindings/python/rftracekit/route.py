"""Route (drive-test) definition and its result wrapper (D2).

A :func:`Route` is an ordered polyline of waypoints sampled by arc length into
receiver positions; :meth:`Simulator.run_route` evaluates each sample and
returns a :class:`RouteResult` whose ordered :class:`RouteSample` series carries
per-sample position, RF metrics, and Doppler.
"""
from __future__ import annotations

from typing import Optional, Sequence

import numpy as np

from . import _native
from ._util import native_of, parse_polarization, require


def Route(
    waypoints: Sequence[Sequence[float]],
    sample_spacing: float = 1.0,
    speed_mps: float = 0.0,
    id: str = "route",
    antenna: Optional["_native.AntennaPattern"] = None,
    polarization=None,
) -> "_native.Route":
    """Build a native :class:`Route` from friendly arguments.

    ``waypoints`` is an ordered sequence of ``[x, y, z]`` points, ``sample_spacing``
    the target arc-length spacing (m) between samples, and ``speed_mps`` an optional
    constant receiver speed used for per-sample Doppler (0 = per-step displacement).
    """
    kwargs = {}
    if antenna is not None:
        kwargs["antenna"] = antenna
    pol = parse_polarization(polarization)
    if pol is not None:
        kwargs["polarization"] = pol
    return _native.Route(
        waypoints=[list(w) for w in waypoints],
        sample_spacing=float(sample_spacing),
        speed_mps=float(speed_mps),
        id=id,
        **kwargs,
    )


class RouteResult:
    """Ordered drive-test result (wraps :class:`_native.RouteResult`).

    Exposes the ordered :attr:`samples`, a lazy pandas :meth:`samples_dataframe`,
    and :meth:`to_json`/:meth:`to_csv` exporters.
    """

    def __init__(self, native: "_native.RouteResult") -> None:
        self.native = native_of(native)

    @property
    def route_id(self) -> str:
        return self.native.route_id

    @property
    def simulation_id(self) -> str:
        return self.native.simulation_id

    @property
    def frequency_hz(self) -> float:
        return self.native.frequency_hz

    @property
    def samples(self):
        """The ordered list of native :class:`RouteSample` objects."""
        return self.native.samples

    # -- numpy accessors ------------------------------------------------------
    @property
    def positions(self) -> np.ndarray:
        """``float64[K, 3]`` array of sample positions in route order."""
        samples = self.native.samples
        if not samples:
            return np.empty((0, 3), dtype=np.float64)
        return np.asarray([s.position for s in samples], dtype=np.float64)

    @property
    def doppler_hz(self) -> np.ndarray:
        """``float64[K]`` array of per-sample aggregate Doppler (Hz)."""
        return np.asarray(
            [s.doppler_hz for s in self.native.samples], dtype=np.float64
        )

    # -- pandas (lazy) --------------------------------------------------------
    def samples_dataframe(self):
        """Return a per-sample :class:`pandas.DataFrame` (pandas is optional)."""
        pd = require("pandas", "data")
        rows = []
        for s in self.native.samples:
            x, y, z = (float(v) for v in s.position)
            rows.append(
                {
                    "index": s.index,
                    "distance_m": s.distance_meters,
                    "x": x,
                    "y": y,
                    "z": z,
                    "has_signal": s.has_signal,
                    "received_power_dbm": s.received_power_dbm,
                    "path_loss_db": s.path_loss_db,
                    "delay_spread_ns": s.delay_spread_ns,
                    "doppler_hz": s.doppler_hz,
                    "serving_transmitter_id": s.serving_transmitter_id,
                    "sinr_db": s.sinr_db,
                    "interference_power_dbm": s.interference_power_dbm,
                }
            )
        return pd.DataFrame(rows)

    # -- exporters ------------------------------------------------------------
    def to_json(self, path: Optional[str] = None) -> Optional[str]:
        """Serialize the route to JSON; write to ``path`` or return the string."""
        if path is None:
            return _native.io.route_to_json_string(self.native)
        _native.io.export_route_json(self.native, str(path))
        return None

    def to_csv(self, path: Optional[str] = None) -> Optional[str]:
        """Serialize the route samples to CSV; write to ``path`` or return it."""
        if path is None:
            return _native.io.route_to_csv_string(self.native)
        _native.io.export_route_csv(self.native, str(path))
        return None

    def __len__(self) -> int:
        return len(self.native.samples)

    def __repr__(self) -> str:
        return (
            f"RouteResult(route_id={self.route_id!r}, "
            f"samples={len(self.native.samples)}, "
            f"frequency_hz={self.frequency_hz:g})"
        )


__all__ = ["Route", "RouteResult"]
