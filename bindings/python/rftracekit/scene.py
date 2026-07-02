"""Ergonomic, keyword-driven wrapper around the native :class:`Scene`."""
from __future__ import annotations

from typing import Optional, Sequence

from . import _native
from ._util import native_of, parse_polarization

Transmitter = _native.Transmitter
Receiver = _native.Receiver
CoordinateSystem = _native.CoordinateSystem
SceneError = _native.SceneError


class Scene:
    """A propagation scene: geometry, materials, transmitters and receivers.

    Wraps :class:`rftracekit._native.Scene` and adds keyword-friendly helpers
    such as :meth:`add_transmitter` and :meth:`add_receiver`. Any native method
    not overridden here is forwarded transparently via ``__getattr__``.
    """

    def __init__(self) -> None:
        self.native = _native.Scene()

    def add_transmitter(
        self,
        id: str,
        position: Sequence[float],
        frequency_hz: float = 3.5e9,
        power_dbm: float = 43.0,
        antenna: Optional["_native.AntennaPattern"] = None,
        polarization=None,
    ) -> "Transmitter":
        """Add a transmitter and return the native object created."""
        tx = self._make_node(
            _native.Transmitter, id, position, antenna, polarization,
            frequency_hz=frequency_hz, power_dbm=power_dbm,
        )
        self.native.add_transmitter(tx)
        return tx

    def add_receiver(
        self,
        id: str,
        position: Sequence[float],
        antenna: Optional["_native.AntennaPattern"] = None,
        polarization=None,
    ) -> "Receiver":
        """Add a receiver and return the native object created."""
        rx = self._make_node(_native.Receiver, id, position, antenna, polarization)
        self.native.add_receiver(rx)
        return rx

    @staticmethod
    def _make_node(cls, id, position, antenna, polarization, **extra):
        kwargs = dict(extra)
        if antenna is not None:
            kwargs["antenna"] = antenna
        pol = parse_polarization(polarization)
        if pol is not None:
            kwargs["polarization"] = pol
        return cls(id, list(position), **kwargs)

    def add_material(self, material) -> int:
        """Register a material and return its index."""
        return self.native.add_material(native_of(material))

    def load_mesh(self, path: str, material: str = "") -> int:
        """Load geometry from ``path``, assigning ``material`` to its faces."""
        return self.native.load_mesh(str(path), material)

    def load_materials(self, path: str) -> int:
        """Load a material library from ``path``; return the number loaded."""
        return self.native.load_materials(str(path))

    def coordinate_system(self) -> "CoordinateSystem":
        return self.native.coordinate_system()

    # -- Georeferencing + geospatial importers (D1) --------------------------
    def set_geo_origin(self, lat_deg: float, lon_deg: float) -> None:
        """Anchor the scene's local ENU frame at a WGS84 origin (degrees)."""
        self.native.set_geo_origin(float(lat_deg), float(lon_deg))

    def has_geo_origin(self) -> bool:
        """True once a geographic origin has been set."""
        return self.native.has_geo_origin()

    def geo_project(self, lat_deg: float, lon_deg: float, alt_meters: float = 0.0):
        """Project a geographic point into local ENU metres (``float64[3]``)."""
        return self.native.geo_project(float(lat_deg), float(lon_deg), float(alt_meters))

    def load_geojson(
        self,
        path: str,
        building_material: str = "concrete",
        point_type: str = "receiver",
    ) -> int:
        """Import a GeoJSON FeatureCollection; return building triangles added."""
        return self.native.load_geojson(str(path), building_material, point_type)

    def load_cityjson(self, path: str, building_material: str = "concrete") -> int:
        """Import a CityJSON document; return the number of triangles added."""
        return self.native.load_cityjson(str(path), building_material)

    def load_osm(
        self,
        path: str,
        building_material: str = "concrete",
        vegetation_material: str = "vegetation",
    ) -> int:
        """Import an OSM/Overpass document; return the number of triangles added."""
        return self.native.load_osm(str(path), building_material, vegetation_material)

    def load_terrain(
        self,
        path: str,
        terrain_material: str = "soil",
        offset_building_bases: bool = False,
    ) -> int:
        """Load a GeoTIFF DEM as terrain (requires a GDAL-enabled build).

        Raises :class:`RuntimeError` when the extension was built without GDAL.
        """
        if not _native.gdal_available():
            raise RuntimeError(
                "load_terrain requires a GDAL-enabled build; this rftracekit "
                "extension was built without GDAL"
            )
        return self.native.load_terrain(
            str(path), terrain_material, offset_building_bases
        )

    def __getattr__(self, name):
        # Only reached for attributes not defined on the wrapper itself.
        return getattr(self.__dict__["native"], name)

    def __repr__(self) -> str:
        return (
            f"Scene(transmitters={len(self.native.transmitters())}, "
            f"receivers={len(self.native.receivers())}, "
            f"materials={len(self.native.materials())})"
        )


__all__ = ["Scene", "Transmitter", "Receiver", "CoordinateSystem", "SceneError"]
