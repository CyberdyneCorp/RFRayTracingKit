"""Thin, wrapper-aware helpers over the native exporters.

Every function accepts either a Python wrapper (:class:`rftracekit.Result` /
:class:`rftracekit.CoverageResult`) or a raw native result, unwrapping as
needed before delegating to ``rftracekit._native.io``.
"""
from __future__ import annotations

from typing import Optional

from .. import _native
from .._util import native_of

_io = _native.io


# -- point-to-point results ---------------------------------------------------
def result_to_json_string(result) -> str:
    return _io.result_to_json_string(native_of(result))


def export_result_json(result, path: str) -> None:
    _io.export_result_json(native_of(result), str(path))


def result_from_json_string(text: str):
    return _io.result_from_json_string(text)


def load_result_json(path: str):
    return _io.load_result_json(str(path))


def receivers_to_csv_string(result) -> str:
    return _io.receivers_to_csv_string(native_of(result))


def export_receivers_csv(result, path: str) -> None:
    _io.export_receivers_csv(native_of(result), str(path))


def receivers_to_geojson_string(result) -> str:
    return _io.receivers_to_geojson_string(native_of(result))


def export_receivers_geojson(result, path: str) -> None:
    _io.export_receivers_geojson(native_of(result), str(path))


def paths_to_geojson_string(result) -> str:
    return _io.paths_to_geojson_string(native_of(result))


def export_paths_geojson(result, path: str) -> None:
    _io.export_paths_geojson(native_of(result), str(path))


def paths_to_gltf_string(result, include_receivers: bool = True) -> str:
    return _io.paths_to_gltf_string(native_of(result), include_receivers)


def export_paths_gltf(result, path: str, include_receivers: bool = True) -> None:
    _io.export_paths_gltf(native_of(result), str(path), include_receivers)


# -- route (drive-test) results -----------------------------------------------
def route_to_json_string(route) -> str:
    return _io.route_to_json_string(native_of(route))


def export_route_json(route, path: str) -> None:
    _io.export_route_json(native_of(route), str(path))


def route_to_csv_string(route) -> str:
    return _io.route_to_csv_string(native_of(route))


def export_route_csv(route, path: str) -> None:
    _io.export_route_csv(native_of(route), str(path))


# -- MIMO channel matrix ------------------------------------------------------
def mimo_to_json_string(channel, snr_linear: float) -> str:
    return _io.mimo_to_json_string(channel, float(snr_linear))


def export_mimo_json(channel, snr_linear: float, path: str) -> None:
    _io.export_mimo_json(channel, float(snr_linear), str(path))


# -- coverage results ---------------------------------------------------------
def coverage_to_json_string(coverage) -> str:
    return _io.coverage_to_json_string(native_of(coverage))


def export_coverage_json(coverage, path: str) -> None:
    _io.export_coverage_json(native_of(coverage), str(path))


def coverage_to_csv_string(coverage) -> str:
    return _io.coverage_to_csv_string(native_of(coverage))


def export_coverage_csv(coverage, path: str) -> None:
    _io.export_coverage_csv(native_of(coverage), str(path))


def coverage_to_geojson_string(coverage) -> str:
    return _io.coverage_to_geojson_string(native_of(coverage))


def export_coverage_geojson(coverage, path: str) -> None:
    _io.export_coverage_geojson(native_of(coverage), str(path))


# -- Cesium CZML / 3D Tiles (D1) ----------------------------------------------
def result_to_czml_string(result, scene=None) -> str:
    if scene is not None:
        return _io.result_to_czml_string(native_of(result), native_of(scene))
    return _io.result_to_czml_string(native_of(result))


def export_result_czml(result, path: str, scene=None) -> None:
    if scene is not None:
        _io.export_result_czml(native_of(result), str(path), native_of(scene))
    else:
        _io.export_result_czml(native_of(result), str(path))


def export_paths_3dtiles(result, directory: str, include_receivers: bool = True) -> None:
    _io.export_paths_3dtiles(native_of(result), str(directory), include_receivers)


# -- GDAL / Parquet-gated exporters -------------------------------------------
def export_coverage_geotiff(
    coverage,
    path: str,
    metric: str = "power",
    origin_lat: float = 0.0,
    origin_lon: float = 0.0,
    georeferenced: bool = False,
) -> None:
    """Coverage -> GeoTIFF heatmap (requires a GDAL-enabled build)."""
    if not _native.gdal_available():
        raise RuntimeError(
            "export_coverage_geotiff requires a GDAL-enabled build; this "
            "rftracekit extension was built without GDAL"
        )
    _io.export_coverage_geotiff(
        native_of(coverage), str(path), metric, origin_lat, origin_lon, georeferenced
    )


def receivers_to_parquet(result, path: str) -> None:
    """Per-receiver table -> Parquet (requires an Apache Arrow/Parquet build)."""
    if not _native.parquet_available():
        raise RuntimeError(
            "receivers_to_parquet requires a Parquet-enabled build; this "
            "rftracekit extension was built without Apache Arrow/Parquet"
        )
    _io.receivers_to_parquet(native_of(result), str(path))


__all__ = [
    "result_to_json_string",
    "export_result_json",
    "result_from_json_string",
    "load_result_json",
    "receivers_to_csv_string",
    "export_receivers_csv",
    "receivers_to_geojson_string",
    "export_receivers_geojson",
    "paths_to_geojson_string",
    "export_paths_geojson",
    "paths_to_gltf_string",
    "export_paths_gltf",
    "route_to_json_string",
    "export_route_json",
    "route_to_csv_string",
    "export_route_csv",
    "mimo_to_json_string",
    "export_mimo_json",
    "coverage_to_json_string",
    "export_coverage_json",
    "coverage_to_csv_string",
    "export_coverage_csv",
    "coverage_to_geojson_string",
    "export_coverage_geojson",
    "result_to_czml_string",
    "export_result_czml",
    "export_paths_3dtiles",
    "export_coverage_geotiff",
    "receivers_to_parquet",
]
