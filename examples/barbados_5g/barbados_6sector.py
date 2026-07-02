#!/usr/bin/env python3
"""6-sector 360 deg 5G site at 7.125 GHz on a 30 m pole, Station Hill Bridgetown.

Six directional sectors spaced 60 deg apart share one 30 m mast at
(13.11220679386025, -59.603538511368605). Reuses the real OSM building/vegetation
scene from barbados_5g.py. Produces, over a 3 km radius:
  * best-server received-power coverage (the combined 360 deg footprint),
  * a serving-sector map (which of the 6 sectors dominates each cell),
  * an inter-sector SINR map (kTB+NF noise), and
  * a 3D close-up of all six radiation-pattern lobes forming the 360 deg flower.

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
import barbados_5g as base  # reuse enu/parse/load_osm/extrude/antenna_balloon

import rftracekit as rf
from rftracekit import _native

FREQ = 7.125e9             # upper 5G mid-band
MAST_H = 30.0
EIRP_DBM = 46.0
DOWNTILT_DEG = 8.0         # steeper tilt: higher band, smaller cell
N_SECTORS = 6
AZIMUTHS = [i * 360.0 / N_SECTORS for i in range(N_SECTORS)]  # 0,60,...,300
SITE_R = 3000.0
GRID_CELL = 30.0
RENDER_R = 380.0
NOISE_BW_HZ = 100e6        # 5G 100 MHz channel
NOISE_FIG_DB = 7.0
HERE = os.path.dirname(os.path.abspath(__file__))
SECTOR_COLORS = ["#e41a1c", "#377eb8", "#4daf4a", "#984ea3", "#ff7f00", "#00ced1"]


def beam_dir(az_deg, dt_deg=DOWNTILT_DEG):
    az, dt = math.radians(az_deg), math.radians(dt_deg)
    return [math.cos(dt) * math.cos(az), math.cos(dt) * math.sin(az), -math.sin(dt)]


def sector_array(az_deg):
    lam = 3e8 / FREQ
    az = math.radians(az_deg)
    ax_x = [-math.sin(az), math.cos(az), 0.0]
    ax_y = [0.0, 0.0, 1.0]
    arr = _native.uniform_planar_array(8, 8, 0.5 * lam, 0.5 * lam, FREQ,
                                       ax_x, ax_y, 3.0)
    arr.boresight = beam_dir(az_deg)
    arr.back_lobe_floor_db = -30.0
    return arr


def sector_tx(i, az_deg):
    tx = _native.Transmitter()
    tx.id = f"sector_{i}"
    tx.position = [0.0, 0.0, MAST_H]
    tx.frequency_hz = FREQ
    tx.power_dbm = EIRP_DBM
    tx.array = sector_array(az_deg)
    tx.beam_steering = beam_dir(az_deg)
    return tx


def scene_with(btris, vtris, transmitters):
    scene = rf.Scene()
    scene.add_material(rf.materials.preset("concrete"))
    scene.add_material(rf.materials.preset("vegetation"))
    scene.add_mesh(btris, "concrete")
    if vtris:
        scene.add_mesh(vtris, "vegetation")
    for tx in transmitters:
        scene.native.add_transmitter(tx)
    return scene


def main():
    t0 = time.time()
    buildings, vegetation = base.load_osm()
    btris, vtris = [], []
    for ring, h in buildings:
        base.extrude(ring, h, btris)
    for ring, h in vegetation:
        base.extrude(ring, h, vtris)
    print(f"[6sector] {len(buildings)} buildings ({len(btris)} tris) loaded in "
          f"{time.time()-t0:.1f}s; {N_SECTORS} sectors @ {FREQ/1e9:.3f} GHz")

    cols = rows = int(2 * SITE_R / GRID_CELL)
    grid = _native.CoverageGrid()
    grid.origin = [-SITE_R, -SITE_R, 0.0]
    grid.cell_size = GRID_CELL
    grid.cols = cols
    grid.rows = rows
    grid.height = 1.5

    settings = rf.SimulationSettings(max_reflections=0)
    settings.enable_vegetation = True

    # One coverage layer per sector (single-tx scene each), reusing the geometry.
    t1 = time.time()
    layers = []
    for i, az in enumerate(AZIMUTHS):
        scene = scene_with(btris, vtris, [sector_tx(i, az)])
        cov = rf.Simulator(settings).run_coverage(scene, grid)
        p = np.array(cov.power_dbm, dtype=float).reshape(rows, cols)
        p[~np.isfinite(p)] = np.nan
        layers.append(p)
    L = np.stack(layers)                       # (6, rows, cols) dBm
    print(f"[6sector] {N_SECTORS} coverage layers in {time.time()-t1:.1f}s")

    any_signal = np.isfinite(L).any(axis=0)
    Lfill = np.where(np.isfinite(L), L, -np.inf)
    serving = np.where(any_signal, np.argmax(Lfill, axis=0), -1)
    best = np.where(any_signal, np.max(Lfill, axis=0), np.nan)

    lin = np.where(np.isfinite(L), 10.0 ** (L / 10.0), 0.0)
    total = lin.sum(axis=0)
    srv = lin.max(axis=0)
    noise = 10.0 ** ((-174.0 + 10 * math.log10(NOISE_BW_HZ) + NOISE_FIG_DB) / 10.0)
    sinr = np.where(any_signal,
                    10.0 * np.log10(srv / (total - srv + noise)), np.nan)

    print(f"[6sector] {int(any_signal.sum())} covered cells; peak {np.nanmax(best):.1f} dBm; "
          f"median SINR {np.nanmedian(sinr):.1f} dB")

    render_maps(best, serving, sinr, buildings)
    render_3d(buildings)
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

    # Best-server received power.
    fig, ax = plt.subplots(figsize=(11, 10))
    im = ax.imshow(best, origin="lower", extent=ext, cmap="turbo", vmin=-110, vmax=-40)
    fig.colorbar(im, ax=ax, shrink=0.8, label="Best received power (dBm)")
    footprints(ax)
    ax.set_title("6-sector 5G @ 7.125 GHz — best-server coverage (3 km)")
    fig.savefig(os.path.join(HERE, "barbados_6sector_coverage.png"), dpi=130, bbox_inches="tight")
    plt.close(fig)

    # Serving-sector map.
    fig, ax = plt.subplots(figsize=(11, 10))
    cmap = ListedColormap(SECTOR_COLORS)
    sm = np.where(serving >= 0, serving, np.nan)
    im = ax.imshow(sm, origin="lower", extent=ext, cmap=cmap,
                   norm=BoundaryNorm(range(N_SECTORS + 1), cmap.N))
    cb = fig.colorbar(im, ax=ax, shrink=0.8, ticks=[i + 0.5 for i in range(N_SECTORS)])
    cb.ax.set_yticklabels([f"S{i} ({int(AZIMUTHS[i])}°)" for i in range(N_SECTORS)])
    cb.set_label("Serving sector")
    footprints(ax)
    ax.set_title("6-sector 5G @ 7.125 GHz — serving-sector (best-server) map")
    fig.savefig(os.path.join(HERE, "barbados_6sector_serving.png"), dpi=130, bbox_inches="tight")
    plt.close(fig)

    # Inter-sector SINR.
    fig, ax = plt.subplots(figsize=(11, 10))
    im = ax.imshow(sinr, origin="lower", extent=ext, cmap="RdYlGn", vmin=-5, vmax=25)
    fig.colorbar(im, ax=ax, shrink=0.8, label="SINR (dB)")
    footprints(ax)
    ax.set_title("6-sector 5G @ 7.125 GHz — inter-sector SINR (100 MHz, NF 7 dB)")
    fig.savefig(os.path.join(HERE, "barbados_6sector_sinr.png"), dpi=130, bbox_inches="tight")
    plt.close(fig)


def render_3d(buildings):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Line3DCollection, Poly3DCollection

    # A ring of receivers around the full 360 deg (every 30 deg, two distances)
    # so every sector illuminates some, plus multipath via ray launch.
    rxs = []
    for adeg in range(0, 360, 30):
        a = math.radians(adeg)
        for d in (170.0, 300.0):
            rxs.append((d * math.cos(a), d * math.sin(a)))

    near = [(r, h) for r, h in buildings
            if min(math.hypot(x, y) for x, y in r) < RENDER_R]
    nb = []
    for ring, h in near:
        base.extrude(ring, h, nb)
    scene = scene_with(nb, [], [sector_tx(i, az) for i, az in enumerate(AZIMUTHS)])
    for j, (x, y) in enumerate(rxs):
        scene.add_receiver(id=f"rx{j}", position=[x, y, 1.5])
    s = rf.SimulationSettings(mode="raylaunch", max_reflections=2,
                              rays_per_transmitter=200_000, capture_radius=5.0, seed=1)
    res = rf.Simulator(s).run(scene)

    fig = plt.figure(figsize=(13, 10))
    ax = fig.add_subplot(111, projection="3d")
    polys = []
    for ring, h in near:
        r = ring[:-1] if ring[0] == ring[-1] else ring
        for i in range(len(r)):
            (x0, y0), (x1, y1) = r[i], r[(i + 1) % len(r)]
            polys.append([(x0, y0, 0), (x1, y1, 0), (x1, y1, h), (x0, y0, h)])
        polys.append([(x, y, h) for x, y in r])
    ax.add_collection3d(Poly3DCollection(polys, facecolor="#c2c6cc",
                        edgecolor="#8a909a", linewidths=0.1, alpha=0.28))

    # Multipath rays colored by received power.
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

    tx = [0.0, 0.0, MAST_H]
    for i, az in enumerate(AZIMUTHS):
        X, Y, Z, G = base.antenna_balloon(sector_array(az), beam_dir(az), tx,
                                          scale=95.0, nlat=48, nlon=96)
        ax.plot_surface(X, Y, Z, color=SECTOR_COLORS[i], rstride=2, cstride=2,
                        linewidth=0, antialiased=True, shade=True, alpha=0.8)
    ax.plot([0, 0], [0, 0], [0, MAST_H], color="0.2", lw=2)  # pole
    ax.scatter(*tx, c="black", marker="*", s=180, depthshade=False,
               label="6-sector site (7.125 GHz, 30 m)")
    ax.scatter([x for x, _ in rxs], [y for _, y in rxs], [1.5] * len(rxs),
               c="#111", s=22, depthshade=False, label="Receivers")
    ax.set_xlim(-RENDER_R, RENDER_R); ax.set_ylim(-RENDER_R, RENDER_R); ax.set_zlim(0, 150)
    ax.set_xlabel("East (m)"); ax.set_ylabel("North (m)"); ax.set_zlabel("z (m)")
    ax.set_title("6-sector 360° site — lobes + multipath rays on a 30 m pole (Station Hill)")
    ax.view_init(elev=32, azim=-60)
    ax.legend(loc="upper left")
    fig.savefig(os.path.join(HERE, "barbados_6sector_3d.png"), dpi=130, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    sys.exit(main())
