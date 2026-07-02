"""Material helpers: presets and an ergonomic constructor."""
from __future__ import annotations

from . import _native

Material = _native.Material


def preset(name: str) -> "Material":
    """Return a built-in material preset (e.g. ``'concrete'``, ``'glass'``)."""
    return _native.materials.preset(name)


def has_preset(name: str) -> bool:
    """Return whether a material preset with ``name`` exists."""
    return _native.materials.has_preset(name)


def create(
    name: str = "default",
    *,
    relative_permittivity: float = 1.0,
    conductivity: float = 0.0,
    roughness: float = 0.0,
    penetration_loss_db: float = 0.0,
    reflection_loss_db: float = 0.0,
) -> "Material":
    """Build a :class:`Material` from keyword parameters."""
    return _native.Material(
        name,
        relative_permittivity,
        conductivity,
        roughness,
        penetration_loss_db,
        reflection_loss_db,
    )


__all__ = ["Material", "preset", "has_preset", "create"]
