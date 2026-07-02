#!/usr/bin/env python3
"""5G sector coverage over Station Hill, Bridgetown, Barbados (3 km radius).

Uses real OpenStreetMap building + vegetation footprints (fetch_osm.py) extruded
into a 3D scene, places a 3.5 GHz beamformed macro sector on a 30 m mast at the
site, and:
  * computes a received-power coverage map over a 3 km radius (buildings block /
    shadow; vegetation attenuates), and
  * renders a top-down coverage heatmap over the city and a 3D close-up showing the
    sector's radiation pattern + multipath rays to nearby receivers.

Prereq:  python3 examples/barbados_5g/fetch_osm.py   (caches osm_bridgetown.json)
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

import rftracekit as rf
from rftracekit import _native

LAT0, LON0 = 13.11220679386025, -59.603538511368605
SITE_R = 3000.0            # coverage radius (m)
MAST_H = 30.0              # antenna height (m)
FREQ = 3.5e9               # 5G mid-band (n78)
EIRP_DBM = 49.0
BEAM_AZ_DEG = 20.0         # sector azimuth (0=east, CCW), toward the open ENE area
BEAM_DOWNTILT_DEG = 4.0
GRID_CELL = 30.0           # coverage cell size (m)
RENDER_R = 420.0           # 3D close-up radius (m)
HERE = os.path.dirname(os.path.abspath(__file__))


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


def extrude(ring, h, tris):
    """Append wall + flat-roof triangles for a footprint ring at height h."""
    r = ring[:-1] if ring[0] == ring[-1] else ring
    n = len(r)
    for i in range(n):
        (x0, y0), (x1, y1) = r[i], r[(i + 1) % n]
        tris.append(_native.Triangle([x0, y0, 0], [x1, y1, 0], [x1, y1, h]))
        tris.append(_native.Triangle([x0, y0, 0], [x1, y1, h], [x0, y0, h]))
    for i in range(1, n - 1):  # roof fan
        tris.append(_native.Triangle([r[0][0], r[0][1], h],
                                     [r[i][0], r[i][1], h],
                                     [r[i + 1][0], r[i + 1][1], h]))


def beam_dir():
    az, dt = math.radians(BEAM_AZ_DEG), math.radians(BEAM_DOWNTILT_DEG)
    return [math.cos(dt) * math.cos(az), math.cos(dt) * math.sin(az), -math.sin(dt)]


def sector_array():
    lam = 3e8 / FREQ
    az = math.radians(BEAM_AZ_DEG)
    ax_x = [-math.sin(az), math.cos(az), 0.0]  # horizontal, across boresight
    ax_y = [0.0, 0.0, 1.0]                     # vertical panel
    arr = _native.uniform_planar_array(8, 8, 0.5 * lam, 0.5 * lam, FREQ,
                                       ax_x, ax_y, 3.0)
    arr.boresight = beam_dir()  # directional panel -> suppress the back lobe
    arr.back_lobe_floor_db = -30.0  # ~30 dB front-to-back, like a real sector
    return arr


def make_transmitter():
    tx = _native.Transmitter()
    tx.id = "5g_sector"
    tx.position = [0.0, 0.0, MAST_H]
    tx.frequency_hz = FREQ
    tx.power_dbm = EIRP_DBM
    tx.array = sector_array()
    tx.beam_steering = beam_dir()
    return tx


def build_scene(buildings, vegetation):
    scene = rf.Scene()
    scene.add_material(rf.materials.preset("concrete"))
    scene.add_material(rf.materials.preset("vegetation"))

    btris = []
    for ring, h in buildings:
        extrude(ring, h, btris)
    scene.add_mesh(btris, "concrete")

    vtris = []
    for ring, h in vegetation:
        extrude(ring, h, vtris)
    if vtris:
        scene.add_mesh(vtris, "vegetation")

    scene.native.add_transmitter(make_transmitter())
    return scene, len(btris), len(vtris)


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


def main():
    t0 = time.time()
    buildings, vegetation = load_osm()
    scene, nb, nv = build_scene(buildings, vegetation)
    print(f"[barbados_5g] {len(buildings)} buildings ({nb} tris), "
          f"{len(vegetation)} veg ({nv} tris); scene built in {time.time()-t0:.1f}s")

    # Coverage over the 3 km radius (LOS + FSPL + sector gain; vegetation attenuates).
    cols = rows = int(2 * SITE_R / GRID_CELL)
    grid = _native.CoverageGrid()
    grid.origin = [-SITE_R, -SITE_R, 0.0]
    grid.cell_size = GRID_CELL
    grid.cols = cols
    grid.rows = rows
    grid.height = 1.5

    s = rf.SimulationSettings(max_reflections=0, simulation_id="barbados_5g")
    s.enable_vegetation = True
    t1 = time.time()
    cov = rf.Simulator(s).run_coverage(scene, grid)
    power = np.array(cov.power_dbm, dtype=float).reshape(rows, cols)
    covered = int(np.isfinite(power).sum())
    print(f"[barbados_5g] coverage {cols}x{rows} cells in {time.time()-t1:.1f}s; "
          f"{covered} cells with signal, peak {np.nanmax(np.where(np.isfinite(power), power, np.nan)):.1f} dBm")

    try:
        cov.to_geojson(os.path.join(HERE, "barbados_coverage.geojson"))
        gj = ", barbados_coverage.geojson"
    except Exception:
        gj = ""
    render_coverage(power, buildings, os.path.join(HERE, "barbados_coverage.png"))
    render_3d(buildings, vegetation, os.path.join(HERE, "barbados_3d.png"))
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
    ax.plot(0, 0, "k^", ms=14, label="5G sector (3.5 GHz, 30 m)")
    # Beam azimuth arrow.
    az = math.radians(BEAM_AZ_DEG)
    ax.arrow(0, 0, 700 * math.cos(az), 700 * math.sin(az), width=25,
             color="magenta", alpha=0.7, length_includes_head=True)
    ax.set_xlim(-SITE_R, SITE_R); ax.set_ylim(-SITE_R, SITE_R)
    ax.set_aspect("equal")
    ax.set_xlabel("East (m)"); ax.set_ylabel("North (m)")
    ax.set_title("RFTraceKit — 5G sector coverage, Station Hill Bridgetown "
                 "(3 km, OSM buildings)")
    ax.legend(loc="upper right")
    fig.savefig(path, dpi=130, bbox_inches="tight")


def render_3d(buildings, vegetation, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Line3DCollection, Poly3DCollection

    # Nearby scene + a small ray-launch run to a few receivers in the beam.
    az = math.radians(BEAM_AZ_DEG)
    rxs = [(d * math.cos(az) + o * -math.sin(az), d * math.sin(az) + o * math.cos(az))
           for d in (120, 220, 320) for o in (-80, 0, 80)]
    scene = rf.Scene()
    scene.add_material(rf.materials.preset("concrete"))
    near = [(r, h) for r, h in buildings
            if min(math.hypot(x, y) for x, y in r) < RENDER_R]
    btris = []
    for ring, h in near:
        extrude(ring, h, btris)
    scene.add_mesh(btris, "concrete")
    scene.native.add_transmitter(make_transmitter())
    for i, (x, y) in enumerate(rxs):
        scene.add_receiver(id=f"rx{i}", position=[x, y, 1.5])
    s = rf.SimulationSettings(mode="raylaunch", max_reflections=2,
                              rays_per_transmitter=600_000, capture_radius=3.0, seed=1)
    res = rf.Simulator(s).run(scene)

    fig = plt.figure(figsize=(14, 9))
    ax = fig.add_subplot(111, projection="3d")
    polys = []
    for ring, h in near:
        r = ring[:-1] if ring[0] == ring[-1] else ring
        for i in range(len(r)):
            (x0, y0), (x1, y1) = r[i], r[(i + 1) % len(r)]
            polys.append([(x0, y0, 0), (x1, y1, 0), (x1, y1, h), (x0, y0, h)])
        polys.append([(x, y, h) for x, y in r])
    ax.add_collection3d(Poly3DCollection(polys, facecolor="#b8bcc0",
                        edgecolor="#7a808a", linewidths=0.1, alpha=0.35))

    segs, cols_, powers = [], [], []
    for r in res.native.receivers:
        for p in r.paths:
            pts = np.asarray(p.points)
            for i in range(len(pts) - 1):
                segs.append([pts[i], pts[i + 1]]); powers.append(p.received_power_dbm)
    if segs:
        import matplotlib.pyplot as plt2
        norm = plt.Normalize(np.percentile(powers, 5), np.percentile(powers, 95))
        cols_ = [plt.cm.RdYlGn(norm(p)) for p in powers]
        ax.add_collection3d(Line3DCollection(segs, colors=cols_, linewidths=0.5, alpha=0.6))

    tx = [0.0, 0.0, MAST_H]
    X, Y, Z, G = antenna_balloon(sector_array(), beam_dir(), tx, scale=110.0)
    gnorm = plt.Normalize(G.min(), G.max())
    ax.plot_surface(X, Y, Z, facecolors=plt.cm.turbo(gnorm(G)), rstride=2, cstride=2,
                    linewidth=0, antialiased=True, shade=False, alpha=0.9)
    ax.scatter(*tx, c="black", marker="*", s=200, depthshade=False,
               label="5G sector (3.5 GHz)")
    ax.scatter([x for x, _ in rxs], [y for _, y in rxs], [1.5] * len(rxs),
               c="#111", s=30, depthshade=False, label="Receivers")
    ax.set_xlim(-RENDER_R, RENDER_R); ax.set_ylim(-RENDER_R, RENDER_R); ax.set_zlim(0, 140)
    ax.set_xlabel("East (m)"); ax.set_ylabel("North (m)"); ax.set_zlabel("z (m)")
    ax.set_title("RFTraceKit — 5G sector beam + multipath, Station Hill (close-up)")
    ax.view_init(elev=24, azim=BEAM_AZ_DEG + 90)
    ax.legend(loc="upper left")
    fig.savefig(path, dpi=130, bbox_inches="tight")


if __name__ == "__main__":
    sys.exit(main())
