"""Result wrappers: numpy views, pandas frames, exporters and plotting."""
from __future__ import annotations

from typing import Optional

import numpy as np

from . import _native
from . import viz as _viz
from ._util import native_of, require


class Result:
    """Point-to-point simulation result (wraps :class:`RFResult`).

    Exposes tabular views (:meth:`receivers_dataframe`, :meth:`paths_dataframe`),
    zero-friction numpy accessors (:attr:`receiver_positions`,
    :attr:`received_power_dbm`, :attr:`path_loss_db`), exporters
    (:meth:`to_json`/:meth:`to_csv`/:meth:`to_geojson`/:meth:`to_gltf`) and
    plotting (:meth:`plot_3d`).
    """

    def __init__(self, native: "_native.RFResult") -> None:
        self.native = native_of(native)

    # -- scalar / structural accessors ---------------------------------------
    @property
    def receivers(self):
        return self.native.receivers

    @property
    def transmitters(self):
        return self.native.transmitters

    @property
    def frequency_hz(self) -> float:
        return self.native.frequency_hz

    @property
    def simulation_id(self) -> str:
        return self.native.simulation_id

    def receiver(self, id: str):
        """Return the :class:`ReceiverResult` for ``id`` (or ``None``)."""
        return self.native.receiver(id)

    @property
    def receiver_ids(self):
        return [r.receiver_id for r in self.native.receivers]

    # -- numpy accessors (D3) -------------------------------------------------
    @property
    def receiver_positions(self) -> np.ndarray:
        """``float64[N, 3]`` array of receiver positions."""
        recs = self.native.receivers
        if not recs:
            return np.empty((0, 3), dtype=np.float64)
        return np.asarray([r.position for r in recs], dtype=np.float64)

    @property
    def received_power_dbm(self) -> np.ndarray:
        """``float64[N]`` array of received power per receiver."""
        return np.asarray(
            [r.received_power_dbm for r in self.native.receivers], dtype=np.float64
        )

    @property
    def path_loss_db(self) -> np.ndarray:
        """``float64[N]`` array of path loss per receiver."""
        return np.asarray(
            [r.path_loss_db for r in self.native.receivers], dtype=np.float64
        )

    def path_geometry(self):
        """Return ``(points, offsets)`` for all paths (D3 layout).

        ``points`` is ``float64[M, 3]`` with every path's vertices concatenated;
        ``offsets`` is ``int32[P + 1]`` such that path ``i`` spans
        ``points[offsets[i]:offsets[i + 1]]``.
        """
        chunks = []
        offsets = [0]
        total = 0
        for rec in self.native.receivers:
            for path in rec.paths:
                pts = np.asarray(path.points, dtype=np.float64).reshape(-1, 3)
                chunks.append(pts)
                total += pts.shape[0]
                offsets.append(total)
        points = (
            np.concatenate(chunks, axis=0)
            if chunks
            else np.empty((0, 3), dtype=np.float64)
        )
        return points, np.asarray(offsets, dtype=np.int32)

    @property
    def path_points(self) -> np.ndarray:
        """All path vertices concatenated as ``float64[M, 3]`` (see D3 / §10.7)."""
        return self.path_geometry()[0]

    @property
    def path_offsets(self) -> np.ndarray:
        """CSR-style ``int32[P + 1]`` offsets into :attr:`path_points`.

        Path ``i`` spans ``path_points[path_offsets[i]:path_offsets[i + 1]]``;
        ``path_offsets[0] == 0`` and ``path_offsets[-1] == len(path_points)``.
        """
        return self.path_geometry()[1]

    # -- pandas (D5, lazy) ----------------------------------------------------
    def receivers_dataframe(self):
        """Return a per-receiver :class:`pandas.DataFrame` (pandas is optional)."""
        pd = require("pandas", "data")
        rows = []
        for r in self.native.receivers:
            x, y, z = (float(v) for v in r.position)
            rows.append(
                {
                    "receiver_id": r.receiver_id,
                    "x": x,
                    "y": y,
                    "z": z,
                    "has_signal": r.has_signal,
                    "received_power_dbm": r.received_power_dbm,
                    "path_loss_db": r.path_loss_db,
                    "phase_rad": r.phase_rad,
                    "delay_spread_ns": r.delay_spread_ns,
                    "num_paths": len(r.paths),
                }
            )
        return pd.DataFrame(rows)

    def paths_dataframe(self):
        """Return a per-path :class:`pandas.DataFrame` (pandas is optional)."""
        pd = require("pandas", "data")
        rows = []
        for r in self.native.receivers:
            for path in r.paths:
                rows.append(
                    {
                        "transmitter_id": path.transmitter_id,
                        "receiver_id": path.receiver_id,
                        "type": path.type.name,
                        "received_power_dbm": path.received_power_dbm,
                        "path_loss_db": path.path_loss_db,
                        "delay_seconds": path.delay_seconds,
                        "phase_rad": path.phase_rad,
                        "reflections": path.reflections,
                        "diffractions": path.diffractions,
                        "num_points": int(
                            np.asarray(path.points).reshape(-1, 3).shape[0]
                        ),
                    }
                )
        return pd.DataFrame(rows)

    # -- exporters ------------------------------------------------------------
    def to_json(self, path: Optional[str] = None) -> Optional[str]:
        """Serialize to JSON; write to ``path`` or return the string."""
        if path is None:
            return _native.io.result_to_json_string(self.native)
        _native.io.export_result_json(self.native, str(path))
        return None

    def to_csv(self, path: Optional[str] = None) -> Optional[str]:
        """Serialize receivers to CSV; write to ``path`` or return the string."""
        if path is None:
            return _native.io.receivers_to_csv_string(self.native)
        _native.io.export_receivers_csv(self.native, str(path))
        return None

    def to_geojson(self, path: Optional[str] = None, kind: str = "receivers"):
        """Serialize receivers or paths to GeoJSON.

        ``kind`` is ``'receivers'`` (default) or ``'paths'``.
        """
        if kind == "receivers":
            to_str, export = (
                _native.io.receivers_to_geojson_string,
                _native.io.export_receivers_geojson,
            )
        elif kind == "paths":
            to_str, export = (
                _native.io.paths_to_geojson_string,
                _native.io.export_paths_geojson,
            )
        else:
            raise ValueError(f"kind must be 'receivers' or 'paths', got {kind!r}")
        if path is None:
            return to_str(self.native)
        export(self.native, str(path))
        return None

    def to_gltf(
        self, path: Optional[str] = None, include_receivers: bool = True
    ) -> Optional[str]:
        """Serialize path geometry to glTF; write to ``path`` or return string."""
        if path is None:
            return _native.io.paths_to_gltf_string(self.native, include_receivers)
        _native.io.export_paths_gltf(self.native, str(path), include_receivers)
        return None

    @classmethod
    def from_json(cls, path: str) -> "Result":
        """Load a result previously written with :meth:`to_json`."""
        return cls(_native.io.load_result_json(str(path)))

    @classmethod
    def from_json_string(cls, text: str) -> "Result":
        """Parse a result from a JSON string."""
        return cls(_native.io.result_from_json_string(text))

    # -- plotting -------------------------------------------------------------
    def plot_3d(self, engine: str = "pyvista", **kwargs):
        """Render transmitters, receivers and paths in 3D (viz is optional)."""
        return _viz.plot_paths(self, engine=engine, **kwargs)

    def __repr__(self) -> str:
        return (
            f"Result(simulation_id={self.simulation_id!r}, "
            f"receivers={len(self.native.receivers)}, "
            f"frequency_hz={self.frequency_hz:g})"
        )


class CoverageResult:
    """Coverage-sweep result (wraps :class:`CoverageResult`)."""

    def __init__(self, native: "_native.CoverageResult") -> None:
        self.native = native_of(native)

    @property
    def grid(self):
        return self.native.grid

    @property
    def frequency_hz(self) -> float:
        return self.native.frequency_hz

    @property
    def simulation_id(self) -> str:
        return self.native.simulation_id

    @property
    def no_signal(self) -> float:
        return self.native.NoSignal

    def power_at(self, row: int, col: int) -> float:
        return self.native.power_at(row, col)

    # -- numpy accessors (D3) -------------------------------------------------
    @property
    def power_dbm(self) -> np.ndarray:
        """Flat ``float64[rows*cols]`` (row-major) received-power array."""
        return np.asarray(self.native.power_dbm, dtype=np.float64)

    @property
    def path_loss_db(self) -> np.ndarray:
        """Flat ``float64[rows*cols]`` (row-major) path-loss array."""
        return np.asarray(self.native.path_loss_db, dtype=np.float64)

    @property
    def coverage_array(self) -> np.ndarray:
        """``float64[rows, cols]`` received-power grid (row-major reshape)."""
        return self.power_dbm.reshape(self.native.grid.rows, self.native.grid.cols)

    @property
    def path_loss_array(self) -> np.ndarray:
        """``float64[rows, cols]`` path-loss grid."""
        return self.path_loss_db.reshape(self.native.grid.rows, self.native.grid.cols)

    def cell_centers(self) -> np.ndarray:
        """``float64[rows, cols, 3]`` world-space cell centers."""
        grid = self.native.grid
        centers = np.empty((grid.rows, grid.cols, 3), dtype=np.float64)
        for r in range(grid.rows):
            for c in range(grid.cols):
                centers[r, c] = np.asarray(grid.cell_center(r, c), dtype=np.float64)
        return centers

    # -- exporters ------------------------------------------------------------
    def to_json(self, path: Optional[str] = None) -> Optional[str]:
        if path is None:
            return _native.io.coverage_to_json_string(self.native)
        _native.io.export_coverage_json(self.native, str(path))
        return None

    def to_csv(self, path: Optional[str] = None) -> Optional[str]:
        if path is None:
            return _native.io.coverage_to_csv_string(self.native)
        _native.io.export_coverage_csv(self.native, str(path))
        return None

    def to_geojson(self, path: Optional[str] = None) -> Optional[str]:
        if path is None:
            return _native.io.coverage_to_geojson_string(self.native)
        _native.io.export_coverage_geojson(self.native, str(path))
        return None

    # -- plotting -------------------------------------------------------------
    def plot_coverage(self, engine: str = "plotly", **kwargs):
        """Render the coverage heatmap (viz is optional)."""
        return _viz.plot_coverage(self, engine=engine, **kwargs)

    def __repr__(self) -> str:
        grid = self.native.grid
        return (
            f"CoverageResult(rows={grid.rows}, cols={grid.cols}, "
            f"cell_size={grid.cell_size:g})"
        )


__all__ = ["Result", "CoverageResult"]
