"""Internal helpers shared across the pure-Python wrappers.

Nothing here is part of the public API; import from ``rftracekit`` instead.
"""
from __future__ import annotations

from . import _native


def native_of(obj):
    """Return the underlying ``_native`` object for a wrapper, or ``obj`` itself.

    Wrappers store their compiled counterpart under ``.native``; native objects
    and other values pass through unchanged. This lets every wrapper accept
    either a wrapped or a raw native instance.
    """
    return getattr(obj, "native", obj)


def parse_backend(backend):
    """Coerce a backend name (``'cpu'``, ``'metal'``, ...) to ``Backend``."""
    if isinstance(backend, _native.Backend):
        return backend
    return _native.backend_from_string(str(backend))


_MODE_ALIASES = {
    "image": _native.PropagationMode.ImageMethod,
    "imagemethod": _native.PropagationMode.ImageMethod,
    "raylaunch": _native.PropagationMode.RayLaunch,
    "ray": _native.PropagationMode.RayLaunch,
    "launch": _native.PropagationMode.RayLaunch,
}


def parse_mode(mode):
    """Coerce a propagation-mode name (``'image'``, ``'raylaunch'``) to enum."""
    if isinstance(mode, _native.PropagationMode):
        return mode
    key = str(mode).strip().lower().replace("_", "").replace("-", "")
    try:
        return _MODE_ALIASES[key]
    except KeyError as exc:
        valid = "'image', 'raylaunch'"
        raise ValueError(f"unknown propagation mode {mode!r}; expected {valid}") from exc


_POLARIZATION_ALIASES = {
    "vertical": _native.Polarization.Vertical,
    "v": _native.Polarization.Vertical,
    "horizontal": _native.Polarization.Horizontal,
    "h": _native.Polarization.Horizontal,
    "rhcp": _native.Polarization.RHCP,
    "lhcp": _native.Polarization.LHCP,
    "none": _native.Polarization.NONE,
}


def parse_polarization(polarization):
    """Coerce a polarization name to ``Polarization`` (pass enums through)."""
    if polarization is None or isinstance(polarization, _native.Polarization):
        return polarization
    key = str(polarization).strip().lower()
    try:
        return _POLARIZATION_ALIASES[key]
    except KeyError as exc:
        valid = "'vertical', 'horizontal', 'rhcp', 'lhcp', 'none'"
        raise ValueError(
            f"unknown polarization {polarization!r}; expected {valid}"
        ) from exc


def require(package, extra):
    """Import an optional dependency, raising a clear error if it is missing."""
    import importlib

    try:
        return importlib.import_module(package)
    except ImportError as exc:  # pragma: no cover - exercised only when absent
        raise ImportError(
            f"{package!r} is required for this feature. "
            f"Install it with: pip install 'rftracekit[{extra}]'"
        ) from exc
