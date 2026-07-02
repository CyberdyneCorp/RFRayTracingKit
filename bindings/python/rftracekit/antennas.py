"""Antenna-pattern helpers."""
from __future__ import annotations

from . import _native

AntennaPattern = _native.AntennaPattern
Polarization = _native.Polarization


def omnidirectional(gain_dbi: float = 0.0) -> "AntennaPattern":
    """Return an omnidirectional antenna pattern with the given peak gain."""
    return _native.AntennaPattern.omnidirectional(gain_dbi)


# Common alias.
omni = omnidirectional


__all__ = ["AntennaPattern", "Polarization", "omnidirectional", "omni"]
