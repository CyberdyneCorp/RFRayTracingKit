#!/usr/bin/env python3
"""6-sector 360 deg 5G site at 7.125 GHz on a 12 m pole on a 16 m building, Station Hill Bridgetown.

Six directional sectors 60 deg apart share one 12 m pole on a 16 m building. Reuses the terrain-aware
OSM scene from barbados_5g.py (buildings on the DEM + a terrain surface). Produces,
over a 3 km radius: best-server coverage, a serving-sector map, an inter-sector
SINR map, and a 3D close-up of all six lobes + multipath over the terrain.

Prereq:  python3 examples/barbados_5g/fetch_osm.py
Run:     PYTHONPATH=bindings/python python3 examples/barbados_5g/barbados_6sector.py
"""
import math
import os
import sys
import time
import warnings

warnings.filterwarnings("ignore")

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import barbados_5g as base

import rftracekit as rf
from rftracekit import _native

FREQ = 7.125e9
MOUNT_BUILDING_H = 16.0     # host building roof height (m)
POLE_H = 12.0               # pole above the roof (m)
MAST_H = MOUNT_BUILDING_H + POLE_H  # antenna 28 m above the host building ground
EIRP_DBM = 46.0
DOWNTILT_DEG = 8.0
N_SECTORS = 6
AZIMUTHS = [i * 360.0 / N_SECTORS for i in range(N_SECTORS)]
SITE_R = 3000.0
GRID_CELL = 30.0
RENDER_R = 380.0
NOISE_BW_HZ = 100e6
NOISE_FIG_DB = 7.0
HERE = os.path.dirname(os.path.abspath(__file__))
SECTOR_COLORS = ["#e41a1c", "#377eb8", "#4daf4a", "#984ea3", "#ff7f00", "#00ced1"]


def beam_dir(az_deg, dt_deg=DOWNTILT_DEG):
    az, dt = math.radians(az_deg), math.radians(dt_deg)
    return [math.cos(dt) * math.cos(az), math.cos(dt) * math.sin(az), -math.sin(dt)]


def sector_array(az_deg):
    lam = 3e8 / FREQ
    az = math.radians(az_deg)
    arr = _native.uniform_planar_array(8, 8, 0.5 * lam, 0.5 * lam, FREQ,
                                       [-math.sin(az), math.cos(az), 0.0],
                                       [0.0, 0.0, 1.0], 3.0)
    arr.boresight = beam_dir(az_deg)
    arr.back_lobe_floor_db = -30.0
    return arr


def sector_tx(i, az_deg, ground_z):
    tx = _native.Transmitter()
    tx.id = f"sector_{i}"
    tx.position = [0.0, 0.0, ground_z + MAST_H]
    tx.frequency_hz = FREQ
    tx.power_dbm = EIRP_DBM
    tx.array = sector_array(az_deg)
    tx.beam_steering = beam_dir(az_deg)
    return tx


def scene_with(btris, vtris, terrain, transmitters):
    scene = rf.Scene()
    scene.add_material(rf.materials.preset("concrete"))
    scene.add_material(rf.materials.preset("vegetation"))
    scene.add_material(rf.materials.preset("soil"))
    scene.add_mesh(terrain, "soil")
    scene.add_mesh(btris, "concrete")
    if vtris:
        scene.add_mesh(vtris, "vegetation")
    for tx in transmitters:
        scene.native.add_transmitter(tx)
    return scene


def main():
    t0 = time.time()
    d = base.get_dem()
    buildings, vegetation = base.load_osm()
    btris, vtris, terrain, site = base.build_geometry(buildings, vegetation, d)
    print(f"[6sector] DEM ground {site:.1f} m; {len(buildings)} buildings "
          f"({len(btris)} tris) + terrain ({len(terrain)} tris) in {time.time()-t0:.1f}s; "
          f"{N_SECTORS} sectors @ {FREQ/1e9:.3f} GHz")

    cols = rows = int(2 * SITE_R / GRID_CELL)
    grid = _native.CoverageGrid()
    grid.origin = [-SITE_R, -SITE_R, 0.0]
    grid.cell_size = GRID_CELL
    grid.cols = cols
    grid.rows = rows
    grid.cell_heights = base.coverage_cell_heights(d, grid, cols, rows)

    settings = rf.SimulationSettings(max_reflections=0)
    settings.enable_vegetation = True

    t1 = time.time()
    layers = []
    for i, az in enumerate(AZIMUTHS):
        scene = scene_with(btris, vtris, terrain, [sector_tx(i, az, site)])
        cov = rf.Simulator(settings).run_coverage(scene, grid)
        p = np.array(cov.power_dbm, dtype=float).reshape(rows, cols)
        p[~np.isfinite(p)] = np.nan
        layers.append(p)
    L = np.stack(layers)
    print(f"[6sector] {N_SECTORS} coverage layers in {time.time()-t1:.1f}s")

    any_sig = np.isfinite(L).any(axis=0)
    Lfill = np.where(np.isfinite(L), L, -np.inf)
    serving = np.where(any_sig, np.argmax(Lfill, axis=0), -1)
    best = np.where(any_sig, np.max(Lfill, axis=0), np.nan)
    lin = np.where(np.isfinite(L), 10.0 ** (L / 10.0), 0.0)
    total = lin.sum(axis=0)
    srv = lin.max(axis=0)
    noise = 10.0 ** ((-174.0 + 10 * math.log10(NOISE_BW_HZ) + NOISE_FIG_DB) / 10.0)
    sinr = np.where(any_sig, 10.0 * np.log10(srv / (total - srv + noise)), np.nan)
    print(f"[6sector] {int(any_sig.sum())} covered cells; peak {np.nanmax(best):.1f} dBm; "
          f"median SINR {np.nanmedian(sinr):.1f} dB")

    render_maps(best, serving, sinr, buildings)
    render_3d(buildings, d, site)
    print("[6sector] wrote barbados_6sector_coverage.png, _serving.png, _sinr.png, _3d.png")
    return 0


def render_maps(best, serving, sinr, buildings):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection
    from matplotlib.colors import ListedColormap, BoundaryNorm

    ext = [-SITE_R, SITE_R, -SITE_R, SITE_R]
    polys = [np.array(r[:-1] if r[0] == r[-1] else r) for r, _ in buildings]

    def footprints(ax):
        ax.add_collection(PolyCollection(polys, facecolors="none",
                          edgecolors="k", linewidths=0.12, alpha=0.3))
        ax.add_patch(plt.Circle((0, 0), SITE_R, fill=False, color="0.4", ls="--", lw=1))
        ax.plot(0, 0, "k^", ms=12)
        ax.set_xlim(-SITE_R, SITE_R); ax.set_ylim(-SITE_R, SITE_R)
        ax.set_aspect("equal"); ax.set_xlabel("East (m)"); ax.set_ylabel("North (m)")

    fig, ax = plt.subplots(figsize=(11, 10))
    im = ax.imshow(best, origin="lower", extent=ext, cmap="turbo", vmin=-110, vmax=-40)
    fig.colorbar(im, ax=ax, shrink=0.8, label="Best received power (dBm)")
    footprints(ax)
    ax.set_title("6-sector 5G @ 7.125 GHz — best-server coverage (3 km, OSM + terrain)")
    fig.savefig(os.path.join(HERE, "barbados_6sector_coverage.png"), dpi=130, bbox_inches="tight")
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(11, 10))
    cmap = ListedColormap(SECTOR_COLORS)
    sm = np.where(serving >= 0, serving, np.nan)
    im = ax.imshow(sm, origin="lower", extent=ext, cmap=cmap,
                   norm=BoundaryNorm(range(N_SECTORS + 1), cmap.N))
    cb = fig.colorbar(im, ax=ax, shrink=0.8, ticks=[i + 0.5 for i in range(N_SECTORS)])
    cb.ax.set_yticklabels([f"S{i} ({int(AZIMUTHS[i])}°)" for i in range(N_SECTORS)])
    cb.set_label("Serving sector")
    footprints(ax)
    ax.set_title("6-sector 5G @ 7.125 GHz — serving-sector map (over terrain)")
    fig.savefig(os.path.join(HERE, "barbados_6sector_serving.png"), dpi=130, bbox_inches="tight")
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(11, 10))
    im = ax.imshow(sinr, origin="lower", extent=ext, cmap="RdYlGn", vmin=-5, vmax=25)
    fig.colorbar(im, ax=ax, shrink=0.8, label="SINR (dB)")
    footprints(ax)
    ax.set_title("6-sector 5G @ 7.125 GHz — inter-sector SINR (100 MHz, NF 7 dB)")
    fig.savefig(os.path.join(HERE, "barbados_6sector_sinr.png"), dpi=130, bbox_inches="tight")
    plt.close(fig)


def render_3d(buildings, d, site):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Line3DCollection, Poly3DCollection

    rxs = []
    for adeg in range(0, 360, 30):
        a = math.radians(adeg)
        for dist in (170.0, 300.0):
            rxs.append((dist * math.cos(a), dist * math.sin(a)))

    near = [(r, h) for r, h in buildings
            if min(math.hypot(x, y) for x, y in r) < RENDER_R]
    terr = base.demmod.terrain_triangles(d, RENDER_R, 30.0, _native.Triangle)
    nb, bz = [], (d.elev_enu(base._centroids(near)[:, 0], base._centroids(near)[:, 1])
                  if near else [])
    for (ring, h), z0 in zip(near, bz):
        base.extrude(ring, float(z0), float(z0) + h, nb)
    scene = scene_with(nb, [], terr, [sector_tx(i, az, site) for i, az in enumerate(AZIMUTHS)])
    rzs = d.elev_enu(np.array([x for x, _ in rxs]), np.array([y for _, y in rxs]))
    for j, ((x, y), rz) in enumerate(zip(rxs, rzs)):
        scene.add_receiver(id=f"rx{j}", position=[x, y, float(rz) + 1.5])
    s = rf.SimulationSettings(mode="raylaunch", max_reflections=2,
                              rays_per_transmitter=200_000, capture_radius=5.0, seed=1)
    res = rf.Simulator(s).run(scene)

    fig = plt.figure(figsize=(13, 10))
    ax = fig.add_subplot(111, projection="3d")
    tp = [[np.asarray(t.v0), np.asarray(t.v1), np.asarray(t.v2)] for t in terr]
    ax.add_collection3d(Poly3DCollection(tp, facecolor="#cdbf9a", edgecolor="none", alpha=0.5))
    polys = []
    for (ring, h), z0 in zip(near, bz):
        r = ring[:-1] if ring[0] == ring[-1] else ring
        z1 = float(z0) + h
        for i in range(len(r)):
            (x0, y0), (x1, y1) = r[i], r[(i + 1) % len(r)]
            polys.append([(x0, y0, z0), (x1, y1, z0), (x1, y1, z1), (x0, y0, z1)])
        polys.append([(x, y, z1) for x, y in r])
    ax.add_collection3d(Poly3DCollection(polys, facecolor="#c2c6cc",
                        edgecolor="#8a909a", linewidths=0.1, alpha=0.35))

    segs, powers = [], []
    for r in res.native.receivers:
        for p in r.paths:
            pts = np.asarray(p.points)
            for i in range(len(pts) - 1):
                segs.append([pts[i], pts[i + 1]]); powers.append(p.received_power_dbm)
    if segs:
        norm = plt.Normalize(np.percentile(powers, 5), np.percentile(powers, 95))
        cols = [plt.cm.RdYlGn(norm(pw)) for pw in powers]
        ax.add_collection3d(Line3DCollection(segs, colors=cols, linewidths=0.4, alpha=0.5))

    tx = [0.0, 0.0, site + MAST_H]
    for i, az in enumerate(AZIMUTHS):
        X, Y, Z, G = base.antenna_balloon(sector_array(az), beam_dir(az), tx,
                                          scale=95.0, nlat=48, nlon=96)
        ax.plot_surface(X, Y, Z, color=SECTOR_COLORS[i], rstride=2, cstride=2,
                        linewidth=0, antialiased=True, shade=True, alpha=0.8)
    ax.plot([0, 0], [0, 0], [site, site + MAST_H], color="0.2", lw=2)
    ax.scatter(*tx, c="black", marker="*", s=180, depthshade=False,
               label="6-sector site (7.125 GHz, 28 m AGL)")
    ax.scatter([x for x, _ in rxs], [y for _, y in rxs],
               [float(z) + 1.5 for z in rzs], c="#111", s=22, depthshade=False,
               label="Receivers")
    ax.set_xlim(-RENDER_R, RENDER_R); ax.set_ylim(-RENDER_R, RENDER_R)
    ax.set_zlim(min(site - 5, 0), site + 150)
    ax.set_xlabel("East (m)"); ax.set_ylabel("North (m)"); ax.set_zlabel("z (m)")
    ax.set_title("6-sector 360° site — lobes + multipath over terrain (Station Hill)")
    ax.view_init(elev=32, azim=-60)
    ax.legend(loc="upper left")
    fig.savefig(os.path.join(HERE, "barbados_6sector_3d.png"), dpi=130, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    sys.exit(main())
