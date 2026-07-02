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
