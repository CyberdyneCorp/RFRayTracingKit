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
    "coverage_to_json_string",
    "export_coverage_json",
    "coverage_to_csv_string",
    "export_coverage_csv",
    "coverage_to_geojson_string",
    "export_coverage_geojson",
]
