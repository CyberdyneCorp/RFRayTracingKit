#!/usr/bin/env python3
"""5G sector coverage over Station Hill, Bridgetown, Barbados (3 km radius).

Uses real OpenStreetMap building + vegetation footprints (fetch_osm.py) AND an
open DEM (AWS terrarium tiles, dem.py) so buildings sit on their real ground
elevation and the terrain surface itself blocks/shadows. Places a 3.5 GHz
beamformed macro sector on a 12 m pole atop a 16 m building (28 m AGL) at the site and:
  * computes a received-power coverage map over a 3 km radius (terrain + buildings
    block; vegetation attenuates), evaluated at terrain height, and
  * renders a top-down coverage heatmap and a 3D close-up with the terrain, the
    sector's radiation pattern, and multipath rays.

Prereq:  python3 examples/barbados_5g/fetch_osm.py
Run:     PYTHONPATH=bindings/python python3 examples/barbados_5g/barbados_5g.py
"""
import json
import math
import os
import sys
import time
import warnings

warnings.filterwarnings("ignore")

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dem as demmod

import rftracekit as rf
from rftracekit import _native

LAT0, LON0 = 13.11220679386025, -59.603538511368605
SITE_R = 3000.0
MOUNT_BUILDING_H = 16.0     # host building roof height (m)
POLE_H = 12.0               # pole above the roof (m)
MAST_H = MOUNT_BUILDING_H + POLE_H  # antenna 28 m above the host building ground
FREQ = 3.5e9
EIRP_DBM = 49.0
BEAM_AZ_DEG = 20.0
BEAM_DOWNTILT_DEG = 4.0
GRID_CELL = 30.0
TERRAIN_STEP = 60.0        # terrain mesh resolution (m)
RENDER_R = 420.0
HERE = os.path.dirname(os.path.abspath(__file__))

_DEM = None


def get_dem():
    global _DEM
    if _DEM is None:
        _DEM = demmod.DEM(LAT0, LON0, SITE_R + 400.0,
                          cache_dir=os.path.join(HERE, "dem_cache"))
    return _DEM


# --- OSM parse + projection --------------------------------------------------
def enu(lat, lon):
    x = (lon - LON0) * 111320.0 * math.cos(math.radians(LAT0))
    y = (lat - LAT0) * 110540.0
    return x, y


def parse_height(tags, default):
    h = tags.get("height")
    if h:
        try:
            return max(3.0, float(str(h).split()[0].replace("m", "")))
        except ValueError:
            pass
    lv = tags.get("building:levels")
    if lv:
        try:
            return max(3.0, float(lv) * 3.2)
        except ValueError:
            pass
    return default


def load_osm():
    with open(os.path.join(HERE, "osm_bridgetown.json")) as f:
        data = json.load(f)
    nodes = {e["id"]: (e["lat"], e["lon"]) for e in data["elements"]
             if e["type"] == "node"}
    buildings, vegetation = [], []
    for e in data["elements"]:
        if e["type"] != "way" or "nodes" not in e:
            continue
        ring = [enu(*nodes[n]) for n in e["nodes"] if n in nodes]
        if len(ring) < 4:
            continue
        tags = e.get("tags", {})
        if "building" in tags:
            buildings.append((ring, parse_height(tags, 6.0)))
        else:
            vegetation.append((ring, 8.0))
    return buildings, vegetation


def extrude(ring, z0, z1, tris):
    """Append wall + flat-roof triangles for a footprint from z0 (ground) to z1."""
    r = ring[:-1] if ring[0] == ring[-1] else ring
    n = len(r)
    for i in range(n):
        (x0, y0), (x1, y1) = r[i], r[(i + 1) % n]
        tris.append(_native.Triangle([x0, y0, z0], [x1, y1, z0], [x1, y1, z1]))
        tris.append(_native.Triangle([x0, y0, z0], [x1, y1, z1], [x0, y0, z1]))
    for i in range(1, n - 1):
        tris.append(_native.Triangle([r[0][0], r[0][1], z1],
                                     [r[i][0], r[i][1], z1],
                                     [r[i + 1][0], r[i + 1][1], z1]))


def _centroids(polys):
    return np.array([np.mean(np.array(r[:-1] if r[0] == r[-1] else r), axis=0)
                     for r, _ in polys])


def build_geometry(buildings, vegetation, d):
    """Terrain-aware geometry: buildings/vegetation sit on the DEM, plus a terrain
    surface mesh. Returns (building_tris, veg_tris, terrain_tris, site_ground_z)."""
    site = float(d.elev_enu(0.0, 0.0))
    btris = []
    if buildings:
        bc = _centroids(buildings)
        bz = d.elev_enu(bc[:, 0], bc[:, 1])
        for (ring, h), z0 in zip(buildings, bz):
            extrude(ring, float(z0), float(z0) + h, btris)
    vtris = []
    if vegetation:
        vc = _centroids(vegetation)
        vz = d.elev_enu(vc[:, 0], vc[:, 1])
        for (ring, h), z0 in zip(vegetation, vz):
            extrude(ring, float(z0), float(z0) + h, vtris)
    terrain = demmod.terrain_triangles(d, SITE_R, TERRAIN_STEP, _native.Triangle)
    return btris, vtris, terrain, site


def beam_dir():
    az, dt = math.radians(BEAM_AZ_DEG), math.radians(BEAM_DOWNTILT_DEG)
    return [math.cos(dt) * math.cos(az), math.cos(dt) * math.sin(az), -math.sin(dt)]


def sector_array():
    lam = 3e8 / FREQ
    az = math.radians(BEAM_AZ_DEG)
    ax_x = [-math.sin(az), math.cos(az), 0.0]
    ax_y = [0.0, 0.0, 1.0]
    arr = _native.uniform_planar_array(8, 8, 0.5 * lam, 0.5 * lam, FREQ,
                                       ax_x, ax_y, 3.0)
    arr.boresight = beam_dir()
    arr.back_lobe_floor_db = -30.0
    return arr


def make_transmitter(ground_z):
    tx = _native.Transmitter()
    tx.id = "5g_sector"
    tx.position = [0.0, 0.0, ground_z + MAST_H]
    tx.frequency_hz = FREQ
    tx.power_dbm = EIRP_DBM
    tx.array = sector_array()
    tx.beam_steering = beam_dir()
    return tx


def build_scene(buildings, vegetation, d):
    btris, vtris, terrain, site = build_geometry(buildings, vegetation, d)
    scene = rf.Scene()
    scene.add_material(rf.materials.preset("concrete"))
    scene.add_material(rf.materials.preset("vegetation"))
    scene.add_material(rf.materials.preset("soil"))
    scene.add_mesh(terrain, "soil")
    scene.add_mesh(btris, "concrete")
    if vtris:
        scene.add_mesh(vtris, "vegetation")
    scene.native.add_transmitter(make_transmitter(site))
    return scene, len(btris), len(terrain), site


def antenna_balloon(array, beam, center, scale, dr_db=30.0, nlat=90, nlon=180):
    th = np.linspace(1e-3, np.pi - 1e-3, nlat)
    ph = np.linspace(0, 2 * np.pi, nlon)
    G = np.empty((nlat, nlon))
    for i, t in enumerate(th):
        st, ct = math.sin(t), math.cos(t)
        for j, f in enumerate(ph):
            G[i, j] = _native.steered_gain_dbi(
                array, beam, [st * math.cos(f), st * math.sin(f), ct])
    r = np.clip((G - (G.max() - dr_db)) / dr_db, 0, 1) * scale
    T, F = np.meshgrid(th, ph, indexing="ij")
    return (center[0] + r * np.sin(T) * np.cos(F),
            center[1] + r * np.sin(T) * np.sin(F),
            center[2] + r * np.cos(T), G)


def coverage_cell_heights(d, grid, cols, rows):
    cx = grid.origin[0] + (np.arange(cols) + 0.5) * GRID_CELL
    cy = grid.origin[1] + (np.arange(rows) + 0.5) * GRID_CELL
    CX, CY = np.meshgrid(cx, cy)  # row-major (rows, cols)
    return (d.elev_enu(CX.ravel(), CY.ravel()) + 1.5).tolist()


def main():
    t0 = time.time()
    d = get_dem()
    buildings, vegetation = load_osm()
    scene, nb, nt, site = build_scene(buildings, vegetation, d)
    print(f"[barbados_5g] DEM ground at site {site:.1f} m; {len(buildings)} buildings "
          f"({nb} tris) + terrain ({nt} tris); scene in {time.time()-t0:.1f}s")

    cols = rows = int(2 * SITE_R / GRID_CELL)
    grid = _native.CoverageGrid()
    grid.origin = [-SITE_R, -SITE_R, 0.0]
    grid.cell_size = GRID_CELL
    grid.cols = cols
    grid.rows = rows
    grid.cell_heights = coverage_cell_heights(d, grid, cols, rows)  # terrain + 1.5

    s = rf.SimulationSettings(max_reflections=0, simulation_id="barbados_5g")
    s.enable_vegetation = True
    t1 = time.time()
    cov = rf.Simulator(s).run_coverage(scene, grid)
    power = np.array(cov.power_dbm, dtype=float).reshape(rows, cols)
    covered = int(np.isfinite(power).sum())
    print(f"[barbados_5g] coverage {cols}x{rows} in {time.time()-t1:.1f}s; "
          f"{covered} cells, peak {np.nanmax(np.where(np.isfinite(power), power, np.nan)):.1f} dBm")

    try:
        cov.to_geojson(os.path.join(HERE, "barbados_coverage.geojson"))
        gj = ", barbados_coverage.geojson"
    except Exception:
        gj = ""
    render_coverage(power, buildings, os.path.join(HERE, "barbados_coverage.png"))
    render_3d(buildings, d, site, os.path.join(HERE, "barbados_3d.png"))
    print("[barbados_5g] wrote barbados_coverage.png, barbados_3d.png" + gj)
    return 0


def render_coverage(power, buildings, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection

    fig, ax = plt.subplots(figsize=(12, 11))
    ext = [-SITE_R, SITE_R, -SITE_R, SITE_R]
    im = ax.imshow(np.where(np.isfinite(power), power, np.nan), origin="lower",
                   extent=ext, cmap="turbo", vmin=-110, vmax=-40, alpha=0.95)
    fig.colorbar(im, ax=ax, shrink=0.8, label="Received power (dBm)")
    polys = [np.array(r[:-1] if r[0] == r[-1] else r) for r, _ in buildings]
    ax.add_collection(PolyCollection(polys, facecolors="none",
                                     edgecolors="k", linewidths=0.15, alpha=0.35))
    ax.add_patch(plt.Circle((0, 0), SITE_R, fill=False, color="white", ls="--", lw=1))
    ax.plot(0, 0, "k^", ms=14, label="5G sector (3.5 GHz, 28 m AGL)")
    az = math.radians(BEAM_AZ_DEG)
    ax.arrow(0, 0, 700 * math.cos(az), 700 * math.sin(az), width=25,
             color="magenta", alpha=0.7, length_includes_head=True)
    ax.set_xlim(-SITE_R, SITE_R); ax.set_ylim(-SITE_R, SITE_R)
    ax.set_aspect("equal")
    ax.set_xlabel("East (m)"); ax.set_ylabel("North (m)")
    ax.set_title("RFTraceKit — 5G sector coverage, Station Hill Bridgetown "
                 "(3 km, OSM + terrain)")
    ax.legend(loc="upper right")
    fig.savefig(path, dpi=130, bbox_inches="tight")


def render_3d(buildings, d, site, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Line3DCollection, Poly3DCollection

    az = math.radians(BEAM_AZ_DEG)
    rxs = [(dd * math.cos(az) + o * -math.sin(az), dd * math.sin(az) + o * math.cos(az))
           for dd in (120, 220, 320) for o in (-80, 0, 80)]
    near = [(r, h) for r, h in buildings
            if min(math.hypot(x, y) for x, y in r) < RENDER_R]

    scene = rf.Scene()
    scene.add_material(rf.materials.preset("concrete"))
    scene.add_material(rf.materials.preset("soil"))
    terr = demmod.terrain_triangles(d, RENDER_R, 30.0, _native.Triangle)
    scene.add_mesh(terr, "soil")
    nb = []
    bz = d.elev_enu(_centroids(near)[:, 0], _centroids(near)[:, 1]) if near else []
    for (ring, h), z0 in zip(near, bz):
        extrude(ring, float(z0), float(z0) + h, nb)
    scene.add_mesh(nb, "concrete")
    scene.native.add_transmitter(make_transmitter(site))
    rzs = d.elev_enu(np.array([x for x, _ in rxs]), np.array([y for _, y in rxs]))
    for i, ((x, y), rz) in enumerate(zip(rxs, rzs)):
        scene.add_receiver(id=f"rx{i}", position=[x, y, float(rz) + 1.5])
    s = rf.SimulationSettings(mode="raylaunch", max_reflections=2,
                              rays_per_transmitter=600_000, capture_radius=3.0, seed=1)
    res = rf.Simulator(s).run(scene)

    fig = plt.figure(figsize=(14, 9))
    ax = fig.add_subplot(111, projection="3d")
    # Terrain surface.
    tp = [[np.asarray(t.v0), np.asarray(t.v1), np.asarray(t.v2)] for t in terr]
    ax.add_collection3d(Poly3DCollection(tp, facecolor="#cdbf9a", edgecolor="none",
                                         alpha=0.5))
    # Buildings.
    polys = []
    for (ring, h), z0 in zip(near, bz):
        r = ring[:-1] if ring[0] == ring[-1] else ring
        z1 = float(z0) + h
        for i in range(len(r)):
            (x0, y0), (x1, y1) = r[i], r[(i + 1) % len(r)]
            polys.append([(x0, y0, z0), (x1, y1, z0), (x1, y1, z1), (x0, y0, z1)])
        polys.append([(x, y, z1) for x, y in r])
    ax.add_collection3d(Poly3DCollection(polys, facecolor="#b8bcc0",
                        edgecolor="#7a808a", linewidths=0.1, alpha=0.4))

    segs, powers = [], []
    for r in res.native.receivers:
        for p in r.paths:
            pts = np.asarray(p.points)
            for i in range(len(pts) - 1):
                segs.append([pts[i], pts[i + 1]]); powers.append(p.received_power_dbm)
    if segs:
        norm = plt.Normalize(np.percentile(powers, 5), np.percentile(powers, 95))
        cols = [plt.cm.RdYlGn(norm(p)) for p in powers]
        ax.add_collection3d(Line3DCollection(segs, colors=cols, linewidths=0.5, alpha=0.6))

    tx = [0.0, 0.0, site + MAST_H]
    X, Y, Z, G = antenna_balloon(sector_array(), beam_dir(), tx, scale=110.0)
    gnorm = plt.Normalize(G.min(), G.max())
    ax.plot_surface(X, Y, Z, facecolors=plt.cm.turbo(gnorm(G)), rstride=2, cstride=2,
                    linewidth=0, antialiased=True, shade=False, alpha=0.9)
    ax.plot([0, 0], [0, 0], [site, site + MAST_H], color="0.2", lw=2)
    ax.scatter(*tx, c="black", marker="*", s=200, depthshade=False,
               label="5G sector (3.5 GHz)")
    ax.scatter([x for x, _ in rxs], [y for _, y in rxs],
               [float(z) + 1.5 for z in rzs], c="#111", s=30, depthshade=False,
               label="Receivers")
    ax.set_xlim(-RENDER_R, RENDER_R); ax.set_ylim(-RENDER_R, RENDER_R)
    ax.set_zlim(min(site - 5, 0), site + 140)
    ax.set_xlabel("East (m)"); ax.set_ylabel("North (m)"); ax.set_zlabel("z (m)")
    ax.set_title("RFTraceKit — 5G sector beam + multipath over terrain (close-up)")
    ax.view_init(elev=24, azim=BEAM_AZ_DEG + 90)
    ax.legend(loc="upper left")
    fig.savefig(path, dpi=130, bbox_inches="tight")


if __name__ == "__main__":
    sys.exit(main())
