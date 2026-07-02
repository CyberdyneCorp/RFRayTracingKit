"""Geospatial IO bindings (D1): importers, CZML/3D-Tiles/GeoTIFF/Parquet export.

Always-on features (GeoJSON/CityJSON/OSM import, MSI antenna, CZML, 3D Tiles)
run in every build. GDAL (terrain, GeoTIFF heatmap) and Parquet (receiver table)
features are skipped cleanly when the extension was built without them.
"""
from __future__ import annotations

import json

import numpy as np
import pytest

import rftracekit as rf
from conftest import basic_scene

# --- fixtures reused from the C++ importer suite (tests/test_geo_import.cpp) --

GEOJSON = """{
  "type":"FeatureCollection","features":[
    {"type":"Feature","properties":{"height":10},
     "geometry":{"type":"Polygon","coordinates":[[[0,0],[0.001,0],[0.001,0.001],[0,0.001],[0,0]]]}},
    {"type":"Feature","properties":{"id":"rx1"},
     "geometry":{"type":"Point","coordinates":[0.0005,0.0005]}}
  ]}"""

CITYJSON = """{
  "type":"CityJSON","version":"1.1",
  "transform":{"scale":[1e-6,1e-6,2.0],"translate":[0,0,5.0]},
  "vertices":[
    [0,0,0],[1000,0,0],[1000,1000,0],[0,1000,0],
    [0,0,1],[1000,0,1],[1000,1000,1],[0,1000,1]],
  "CityObjects":{
    "b1":{"type":"Building","geometry":[
      {"type":"MultiSurface","lod":"2","boundaries":[
        [[0,1,2,3]],[[4,5,6,7]],
        [[0,1,5,4]],[[1,2,6,5]],[[2,3,7,6]],[[3,0,4,7]]
      ]}
    ]}
  }}"""

OSM_JSON = """{
  "version":0.6,"elements":[
    {"type":"node","id":1,"lat":0.0,"lon":0.0},
    {"type":"node","id":2,"lat":0.0,"lon":0.001},
    {"type":"node","id":3,"lat":0.001,"lon":0.001},
    {"type":"node","id":4,"lat":0.001,"lon":0.0},
    {"type":"node","id":5,"lat":0.002,"lon":0.0},
    {"type":"node","id":6,"lat":0.002,"lon":0.001},
    {"type":"node","id":7,"lat":0.003,"lon":0.001},
    {"type":"node","id":8,"lat":0.003,"lon":0.0},
    {"type":"way","id":100,"nodes":[1,2,3,4,1],
     "tags":{"building":"yes","height":"12"}},
    {"type":"way","id":101,"nodes":[5,6,7,8,5],
     "tags":{"natural":"wood"}}
  ]}"""

MSI = """NAME Test Sector Antenna
FREQUENCY 900
GAIN 15.0 dBd
TILT 0
HORIZONTAL 5
0 0.0
45 5.0
90 10.0
135 15.0
180 20.0
VERTICAL 5
0 0.0
45 3.0
90 6.0
135 9.0
180 12.0
"""


def _write(tmp_path, name, text):
    path = tmp_path / name
    path.write_text(text)
    return str(path)


def _link_result():
    return rf.Simulator(
        rf.SimulationSettings(mode="image", max_reflections=1)
    ).run(basic_scene())


# --- importers (always-on) ---------------------------------------------------

def test_load_geojson_extrudes_building_and_adds_receiver(tmp_path):
    scene = rf.Scene()
    scene.set_geo_origin(0.0, 0.0)  # deterministic equirectangular projection
    tris = scene.load_geojson(_write(tmp_path, "b.geojson", GEOJSON))
    assert tris == 10  # 4-corner footprint -> 8 walls + 2 roof triangles

    recs = scene.receivers()
    assert len(recs) == 1
    assert recs[0].id == "rx1"
    # x = 0.0005 deg * 111320 m/deg east.
    assert recs[0].position[0] == pytest.approx(0.0005 * 111320.0, rel=1e-6)


def test_load_cityjson_triangulates_building(tmp_path):
    scene = rf.Scene()
    scene.set_geo_origin(0.0, 0.0)
    tris = scene.load_cityjson(_write(tmp_path, "b.city.json", CITYJSON))
    assert tris == 12  # 6 quad faces -> 12 triangles


def test_load_osm_json_buildings_and_vegetation(tmp_path):
    scene = rf.Scene()
    scene.set_geo_origin(0.0, 0.0)
    tris = scene.load_osm(_write(tmp_path, "city.osm.json", OSM_JSON))
    assert tris == 20  # one building + one vegetation footprint, 10 tris each
    # Both the building and the vegetation material are registered.
    names = {m.name for m in scene.materials()}
    assert "concrete" in names and "vegetation" in names


def test_set_and_project_geo_origin():
    scene = rf.Scene()
    assert not scene.has_geo_origin()
    scene.set_geo_origin(0.0, 0.0)
    assert scene.has_geo_origin()

    p = scene.geo_project(0.0, 0.001, 5.0)
    assert isinstance(p, np.ndarray) and p.shape == (3,)
    assert p[0] == pytest.approx(0.001 * 111320.0, rel=1e-6)
    assert p[1] == pytest.approx(0.0, abs=1e-6)
    assert p[2] == pytest.approx(5.0)


def test_load_msi_antenna_returns_directional_pattern(tmp_path):
    pat = rf.load_msi_antenna(_write(tmp_path, "sector.msi", MSI))
    assert isinstance(pat, rf.AntennaPattern)
    assert not pat.omni
    assert pat.peak_gain_dbi == pytest.approx(17.15)  # 15.0 dBd + 2.15


# --- always-on exporters -----------------------------------------------------

def test_to_czml_string_and_file(tmp_path):
    r = _link_result()
    doc = json.loads(r.to_czml())
    assert isinstance(doc, list) and doc  # CZML is a JSON array of packets
    assert doc[0].get("id") == "document"

    path = tmp_path / "scene.czml"
    assert r.to_czml(str(path)) is None
    assert path.exists() and path.stat().st_size > 0
    assert isinstance(json.loads(path.read_text()), list)


def test_to_czml_scene_georeferenced(tmp_path):
    scene = rf.Scene()
    scene.set_geo_origin(10.0, 50.0)
    scene.add_transmitter(id="tx", position=[0, 0, 30])
    scene.add_receiver(id="rx", position=[100, 0, 1.5])
    r = rf.Simulator(rf.SimulationSettings(mode="image")).run(scene)
    # Scene overload must emit valid CZML (cartographicDegrees when georeferenced).
    doc = json.loads(r.to_czml(scene=scene))
    assert isinstance(doc, list) and doc[0].get("id") == "document"


def test_to_3dtiles_writes_valid_tileset(tmp_path):
    r = _link_result()
    out = tmp_path / "tiles"
    r.to_3dtiles(str(out))

    tileset_path = out / "tileset.json"
    assert tileset_path.exists()
    tileset = json.loads(tileset_path.read_text())
    assert tileset["asset"]["version"] == "1.1"
    root = tileset["root"]
    assert "boundingVolume" in root
    assert isinstance(root["geometricError"], (int, float))
    uri = root["content"]["uri"]
    assert (out / uri).exists() and (out / uri).stat().st_size > 0


def test_availability_helpers_are_bool():
    assert isinstance(rf.gdal_available(), bool)
    assert isinstance(rf.parquet_available(), bool)


# --- GDAL-gated: coverage GeoTIFF + terrain import ---------------------------

@pytest.mark.skipif(not rf.gdal_available(), reason="extension built without GDAL")
def test_coverage_to_geotiff(tmp_path):
    rasterio = pytest.importorskip("rasterio")
    scene = rf.Scene()
    scene.add_transmitter(id="tx", position=[20, 20, 20], power_dbm=43.0)
    grid = rf.make_grid(cell_size=20.0, cols=3, rows=3)
    cov = rf.Simulator(rf.SimulationSettings(mode="image", max_reflections=0)).run_coverage(
        scene, grid
    )
    path = tmp_path / "coverage.tif"
    cov.to_geotiff(str(path), metric="power")
    assert path.exists() and path.stat().st_size > 0
    with rasterio.open(str(path)) as ds:
        assert ds.width == 3 and ds.height == 3 and ds.count == 1


@pytest.mark.skipif(not rf.gdal_available(), reason="extension built without GDAL")
def test_load_terrain_from_geotiff_dem(tmp_path):
    rasterio = pytest.importorskip("rasterio")
    from rasterio.transform import from_origin

    w, h, pix = 4, 3, 0.001
    data = np.fromfunction(lambda r, c: 100.0 + r * 10.0 + c, (h, w), dtype=np.float64)
    path = tmp_path / "dem.tif"
    with rasterio.open(
        str(path), "w", driver="GTiff", width=w, height=h, count=1,
        dtype="float64", crs="EPSG:4326",
        transform=from_origin(10.0, 50.0, pix, pix),
    ) as dst:
        dst.write(data, 1)

    scene = rf.Scene()
    tris = scene.load_terrain(str(path))
    assert tris > 0  # DEM triangulated into a terrain surface
    assert scene.has_geo_origin()  # loadTerrain anchors the georeference


# --- Parquet-gated: receiver table -------------------------------------------

@pytest.mark.skipif(
    not rf.parquet_available(), reason="extension built without Apache Arrow/Parquet"
)
def test_receivers_to_parquet(tmp_path):
    pq = pytest.importorskip("pyarrow.parquet")
    r = _link_result()
    path = tmp_path / "receivers.parquet"
    r.to_parquet(str(path))
    assert path.exists() and path.stat().st_size > 0

    table = pq.read_table(str(path))
    cols = set(table.column_names)
    assert {"id", "x", "y", "z", "received_power_dbm"}.issubset(cols)
    assert table.num_rows == len(r.receivers)


# --- graceful degradation of the pure-Python wrappers ------------------------

def test_gated_wrappers_raise_clear_error_when_unavailable():
    r = _link_result()
    if not rf.parquet_available():
        with pytest.raises(RuntimeError, match="Parquet"):
            r.to_parquet("unused.parquet")
    if not rf.gdal_available():
        scene = rf.Scene()
        with pytest.raises(RuntimeError, match="GDAL"):
            scene.load_terrain("unused.tif")
