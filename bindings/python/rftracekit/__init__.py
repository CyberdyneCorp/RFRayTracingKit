"""RFTraceKit — Python bindings for the C++ RF ray-tracing / propagation core.

Typical use::

    import rftracekit as rf

    scene = rf.Scene()
    scene.add_transmitter(id="tx", position=[0, 0, 30],
                          frequency_hz=3.5e9, power_dbm=43)
    scene.add_receiver(id="rx", position=[100, 0, 1.5])

    result = rf.Simulator(rf.SimulationSettings(mode="raylaunch")).run(scene)
    print(result.received_power_dbm)          # numpy array

The compiled extension lives at :mod:`rftracekit._native`; everything exported
here is a thin, snake-case, numpy/pandas-friendly wrapper around it. Optional
visualization (pyvista/plotly) and pandas are imported lazily, so importing this
package never requires them.
"""
from __future__ import annotations

from . import _native

# Enums and value types re-exported straight from the extension.
from ._native import (
    AntennaPattern,
    Backend,
    CoordinateSystem,
    DiffractionModel,
    PathType,
    Polarization,
    PropagationMode,
    RFPath,
    RFResult,
    ReceiverResult,
    Triangle,
    TransmitterInfo,
    UpAxis,
    backend_available,
    backend_from_string,
    backend_to_string,
    gdal_available,
    load_msi_antenna,
    parquet_available,
)

# Submodules (all import-safe without optional deps).
from . import antennas, io, materials, mimo, viz

# Ergonomic wrappers.
from .antennas import omni, omnidirectional
from .materials import Material
from .results import CoverageResult, Result
from .route import Route, RouteResult
from .scene import Receiver, Scene, SceneError, Transmitter
from .simulator import CoverageGrid, Simulator, SimulationSettings, make_grid

__version__ = "0.3.0"

__all__ = [
    # scene / entities
    "Scene",
    "Transmitter",
    "Receiver",
    "SceneError",
    "CoordinateSystem",
    "UpAxis",
    # materials / antennas
    "Material",
    "materials",
    "AntennaPattern",
    "antennas",
    "omnidirectional",
    "omni",
    "Polarization",
    # simulation
    "SimulationSettings",
    "Simulator",
    "CoverageGrid",
    "make_grid",
    "Route",
    "RouteResult",
    "mimo",
    "PropagationMode",
    "Backend",
    "DiffractionModel",
    "backend_from_string",
    "backend_to_string",
    "backend_available",
    # geospatial IO (D1)
    "load_msi_antenna",
    "gdal_available",
    "parquet_available",
    # results
    "Result",
    "CoverageResult",
    "RFResult",
    "ReceiverResult",
    "RFPath",
    "TransmitterInfo",
    "PathType",
    "Triangle",
    # io / viz
    "io",
    "viz",
    # escape hatch
    "_native",
    "__version__",
]
