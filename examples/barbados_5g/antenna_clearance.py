#!/usr/bin/env python3
"""Antenna-clearance check for the Station Hill site.

Mounting: a 12 m pole on a 16 m building -> antenna 28 m above the building's
ground. Using the DEM (terrain) + OSM building heights, checks whether the antenna
clears every *immediate* building (within R metres) by at least a target margin,
accounting for the fact that nearby buildings sit on their own terrain elevation.

Run:  PYTHONPATH=bindings/python python3 examples/barbados_5g/antenna_clearance.py
"""
import math
import os
import sys
import warnings

warnings.filterwarnings("ignore")

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import barbados_5g as base

MOUNT_BUILDING_H = 16.0     # host building roof height (m, given)
POLE_H = 12.0               # pole above the roof (m, given)
R_IMMEDIATE = 150.0         # "immediate surroundings" radius (m)
TARGET_MARGIN = 6.0         # required clearance above neighbouring rooftops (m)
HERE = os.path.dirname(os.path.abspath(__file__))


def ring_xy(r):
    return np.array(r[:-1] if r[0] == r[-1] else r)


def point_in_ring(px, py, ring):
    inside = False
    n = len(ring)
    for i in range(n):
        x0, y0 = ring[i]
        x1, y1 = ring[(i + 1) % n]
        if (y0 > py) != (y1 > py):
            xi = x0 + (py - y0) * (x1 - x0) / (y1 - y0)
            if px < xi:
                inside = not inside
    return inside


def main():
    d = base.get_dem()
    buildings, _ = base.load_osm()
    ground = float(d.elev_enu(0.0, 0.0))               # terrain at the site
    antenna_abs = ground + MOUNT_BUILDING_H + POLE_H    # antenna absolute elevation

    cents = base._centroids(buildings)
    dist = np.hypot(cents[:, 0], cents[:, 1])
    # Host building = footprint containing the site point (fallback: nearest).
    host = None
    for i, (ring, _) in enumerate(buildings):
        if point_in_ring(0.0, 0.0, ring_xy(ring)):
            host = i
            break
    if host is None:
        host = int(np.argmin(dist))

    terr = d.elev_enu(cents[:, 0], cents[:, 1])
    rooftop_abs = terr + np.array([h for _, h in buildings])   # ground + OSM height
    clearance = antenna_abs - rooftop_abs

    imm = np.where((dist <= R_IMMEDIATE) & (np.arange(len(buildings)) != host))[0]
    print(f"site ground elevation      : {ground:.1f} m")
    print(f"antenna absolute elevation : {antenna_abs:.1f} m  "
          f"(= {ground:.1f} + {MOUNT_BUILDING_H:.0f} roof + {POLE_H:.0f} pole)")
    print(f"immediate buildings (<= {R_IMMEDIATE:.0f} m): {len(imm)}")
    if len(imm) == 0:
        return 0
    cl = clearance[imm]
    tallest = imm[np.argmax(rooftop_abs[imm])]
    worst = imm[np.argmin(cl)]
    print(f"tallest neighbour rooftop  : {rooftop_abs[tallest]:.1f} m "
          f"(terrain {terr[tallest]:.1f} + {buildings[tallest][1]:.0f} m, "
          f"{dist[tallest]:.0f} m away)")
    print(f"min clearance to a rooftop : {cl.min():.1f} m "
          f"(at {dist[worst]:.0f} m; rooftop {rooftop_abs[worst]:.1f} m)")
    below = imm[clearance[imm] < TARGET_MARGIN]
    print(f"neighbours within {TARGET_MARGIN:.0f} m of / above the antenna: {len(below)}")
    ok = cl.min() >= TARGET_MARGIN
    print(f"\n=> antenna is {'AT LEAST' if ok else 'NOT'} {TARGET_MARGIN:.0f} m above all "
          f"immediate buildings (min margin {cl.min():.1f} m).")
    print("\nNOTE: most OSM footprints here are untagged -> a 6 m default height, so\n"
          "rooftop estimates for neighbours are conservative/approximate. The host\n"
          "building height (16 m) and pole (12 m) are the given values.")

    render(buildings, imm, clearance, dist, os.path.join(HERE, "barbados_clearance.png"))
    print("wrote barbados_clearance.png")
    return 0


def render(buildings, imm, clearance, dist, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection

    R = R_IMMEDIATE + 40
    fig, ax = plt.subplots(figsize=(9, 9))
    polys = [ring_xy(buildings[i][0]) for i in imm]
    cl = clearance[imm]
    cmap = plt.cm.RdYlGn
    norm = plt.Normalize(0, 25)
    pc = PolyCollection(polys, array=cl, cmap=cmap, norm=norm,
                        edgecolors="k", linewidths=0.3)
    ax.add_collection(pc)
    fig.colorbar(pc, ax=ax, shrink=0.8, label="Clearance below antenna (m)")
    ax.add_patch(plt.Circle((0, 0), R_IMMEDIATE, fill=False, color="0.4", ls="--"))
    ax.plot(0, 0, "k*", ms=18, label=f"Antenna (roof {MOUNT_BUILDING_H:.0f} m + pole {POLE_H:.0f} m)")
    ax.set_xlim(-R, R); ax.set_ylim(-R, R); ax.set_aspect("equal")
    ax.set_xlabel("East (m)"); ax.set_ylabel("North (m)")
    ax.set_title("Immediate-building clearance below the antenna (Station Hill)")
    ax.legend(loc="upper right")
    fig.savefig(path, dpi=130, bbox_inches="tight")


if __name__ == "__main__":
    sys.exit(main())
