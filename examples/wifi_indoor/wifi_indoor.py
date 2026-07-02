#!/usr/bin/env python3
"""Indoor Wi-Fi (2.4 GHz) ray-tracing demo with a directional antenna pattern.

Builds a detailed two-room office (concrete shell, glass facade, wood cubicles/
desks, a metal cabinet), mounts a ceiling Wi-Fi AP with a 4x4 planar antenna
array steered downward, runs stochastic ray-launch multipath on the CPU engine,
then:
  * exports ray paths to glTF (colored by received power) + receivers to GeoJSON,
  * renders a 3D view (matplotlib PNG + interactive Plotly HTML) showing the
    material-colored scene, the multipath rays, and the transmitter's radiation
    pattern "balloon" sampled from the SAME array-factor gain the physics uses.

Run:  PYTHONPATH=bindings/python python3 examples/wifi_indoor/wifi_indoor.py
"""
import os
import sys
import warnings

warnings.filterwarnings("ignore")  # silence unrelated numpy/pandas plugin noise

import numpy as np

import rftracekit as rf
from rftracekit import _native

# Material -> render color (glass cyan echoes the reference visualization).
MAT_COLOR = {"concrete": "#b8bcc0", "glass": "#79d2e6",
             "wood": "#b5895a", "metal": "#5a5f66"}


# --- geometry helpers --------------------------------------------------------
def quad(a, b, c, d):
    return [_native.Triangle(a, b, c), _native.Triangle(a, c, d)]


def box(x0, y0, z0, x1, y1, z1):
    p = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    faces = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4),
             (2, 3, 7, 6), (1, 2, 6, 5), (0, 3, 7, 4)]
    return [t for f in faces for t in quad(p[f[0]], p[f[1]], p[f[2]], p[f[3]])]


def build_scene():
    scene = rf.Scene()
    for name in MAT_COLOR:
        scene.add_material(rf.materials.preset(name))

    meshes = []  # (triangles, material) kept for rendering

    def mesh(triangles, material):
        scene.add_mesh(triangles, material)
        meshes.append((triangles, material))

    W, D, H = 14.0, 10.0, 3.0
    # Room shell: concrete floor/ceiling/walls, a glass facade on y=D.
    mesh(quad((0, 0, 0), (W, 0, 0), (W, D, 0), (0, D, 0)), "concrete")   # floor
    mesh(quad((0, 0, H), (W, 0, H), (W, D, H), (0, D, H)), "concrete")   # ceiling
    mesh(quad((0, 0, 0), (W, 0, 0), (W, 0, H), (0, 0, H)), "concrete")
    mesh(quad((0, D, 0), (W, D, 0), (W, D, H), (0, D, H)), "glass")      # facade
    mesh(quad((0, 0, 0), (0, D, 0), (0, D, H), (0, 0, H)), "concrete")
    mesh(quad((W, 0, 0), (W, D, 0), (W, D, H), (W, 0, H)), "concrete")

    # Interior dividing wall at x=7 with a 2 m doorway (y in [4,6]).
    mesh(quad((7, 0, 0), (7, 4, 0), (7, 4, H), (7, 0, H)), "concrete")
    mesh(quad((7, 6, 0), (7, D, 0), (7, D, H), (7, 6, H)), "concrete")
    mesh(quad((7, 4, 2.2), (7, 6, 2.2), (7, 6, H), (7, 4, H)), "concrete")  # lintel

    # Cubicle partitions (wood), desks (wood), a metal filing cabinet, a pillar.
    mesh(box(3.0, 1.0, 0.0, 3.15, 5.0, 1.6), "wood")
    mesh(box(1.0, 5.0, 0.0, 4.0, 5.15, 1.6), "wood")
    mesh(box(1.0, 1.2, 0.0, 3.0, 2.4, 0.75), "wood")     # desk
    mesh(box(9.5, 2.0, 0.0, 12.5, 3.2, 0.75), "wood")    # desk
    mesh(box(9.5, 6.5, 0.0, 12.5, 7.7, 0.75), "wood")    # desk
    mesh(box(11.6, 8.4, 0.0, 12.4, 9.4, 1.3), "metal")   # filing cabinet
    mesh(box(10.0, 4.7, 0.0, 10.5, 5.3, H), "concrete")  # pillar

    # Wi-Fi AP: ceiling-mounted 4x4 planar array @ 2.4 GHz, steered straight down.
    lam = 3e8 / 2.4e9
    tx = _native.Transmitter()
    tx.id = "ap"
    tx.position = [3.5, 5.0, 2.9]
    tx.frequency_hz = 2.4e9
    tx.power_dbm = 20.0
    tx.array = _native.uniform_planar_array(4, 4, 0.5 * lam, 0.5 * lam, 2.4e9,
                                            [1, 0, 0], [0, 1, 0], 0.0)
    tx.beam_steering = [0, 0, -1]
    scene.native.add_transmitter(tx)

    rx = [(2.0, 2.0, 1.0), (5.5, 2.0, 1.0), (2.0, 8.0, 1.0), (5.5, 8.0, 1.0),
          (6.0, 5.0, 1.0), (9.5, 2.5, 1.0), (12.0, 3.0, 1.0), (11.0, 5.0, 1.0),
          (10.0, 7.0, 1.0), (12.5, 8.5, 1.0)]
    for i, p in enumerate(rx):
        scene.add_receiver(id=f"rx_{i:02d}", position=list(p))

    return (scene, meshes, np.array(tx.position), tx.array,
            np.array([0, 0, -1.0]), np.array(rx, dtype=float))


def antenna_balloon(array, beam, center, scale=1.6, nlat=54, nlon=108):
    """Sample the array-factor gain over a sphere -> a radiation-pattern surface."""
    th = np.linspace(1e-3, np.pi - 1e-3, nlat)
    ph = np.linspace(0, 2 * np.pi, nlon)
    G = np.empty((nlat, nlon))
    beam = list(beam)
    for i, t in enumerate(th):
        st, ct = np.sin(t), np.cos(t)
        for j, f in enumerate(ph):
            G[i, j] = _native.steered_gain_dbi(
                array, beam, [st * np.cos(f), st * np.sin(f), ct])
    gmin = np.percentile(G, 10)
    r = np.clip((G - gmin) / (G.max() - gmin), 0.0, 1.0) * scale
    T, F = np.meshgrid(th, ph, indexing="ij")
    X = center[0] + r * np.sin(T) * np.cos(F)
    Y = center[1] + r * np.sin(T) * np.sin(F)
    Z = center[2] + r * np.cos(T)
    return X, Y, Z, G


def main():
    scene, meshes, tx_pos, tx_array, tx_beam, rx_pos = build_scene()

    settings = rf.SimulationSettings(
        mode="raylaunch", max_reflections=2,
        rays_per_transmitter=600_000, capture_radius=0.75, seed=1)
    result = rf.Simulator(settings).run(scene)

    outdir = os.path.dirname(os.path.abspath(__file__))
    result.to_gltf(os.path.join(outdir, "wifi_paths.gltf"))
    result.to_geojson(os.path.join(outdir, "wifi_receivers.geojson"), kind="receivers")

    polylines, powers, reached = [], [], 0
    for r in result.native.receivers:
        reached += 1 if r.has_signal else 0
        for p in r.paths:
            polylines.append(np.asarray(p.points, dtype=float))
            powers.append(p.received_power_dbm)
    print(f"[wifi_indoor] {sum(len(t) for t, _ in meshes)} triangles, "
          f"4x4 array AP @ 2.4 GHz, {len(rx_pos)} receivers, {reached} reached, "
          f"{len(polylines)} captured paths")
    if not polylines:
        print("no captured paths; increase capture_radius or rays")
        return 1

    balloon = antenna_balloon(tx_array, tx_beam, tx_pos)
    render_mpl(meshes, tx_pos, rx_pos, polylines, powers, balloon,
               os.path.join(outdir, "wifi_indoor.png"))
    try:
        render_plotly(meshes, tx_pos, rx_pos, polylines, powers, balloon,
                      os.path.join(outdir, "wifi_indoor.html"))
        extra = " + wifi_indoor.html"
    except Exception as e:
        extra = f"  (HTML skipped: {e})"
    print("[wifi_indoor] wrote wifi_paths.gltf, wifi_receivers.geojson, "
          "wifi_indoor.png" + extra)
    return 0


def render_mpl(meshes, tx_pos, rx_pos, polylines, powers, balloon, png_path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Line3DCollection, Poly3DCollection

    fig = plt.figure(figsize=(14, 9))
    ax = fig.add_subplot(111, projection="3d")

    for tris, mat in meshes:
        polys = [[np.asarray(t.v0), np.asarray(t.v1), np.asarray(t.v2)] for t in tris]
        ax.add_collection3d(Poly3DCollection(
            polys, facecolor=MAT_COLOR[mat], edgecolor="#6b7078",
            linewidths=0.15, alpha=0.16 if mat != "glass" else 0.22))

    p = np.array(powers)
    norm = plt.Normalize(np.percentile(p, 5), np.percentile(p, 95))
    cmap = plt.cm.RdYlGn
    segs = [[pts[i], pts[i + 1]] for pts in polylines for i in range(len(pts) - 1)]
    cols = [cmap(norm(pw)) for pts, pw in zip(polylines, powers)
            for _ in range(len(pts) - 1)]
    ax.add_collection3d(Line3DCollection(segs, colors=cols, linewidths=0.5, alpha=0.55))

    # Radiation-pattern balloon at the transmitter.
    X, Y, Z, G = balloon
    gnorm = plt.Normalize(G.min(), G.max())
    ax.plot_surface(X, Y, Z, facecolors=plt.cm.turbo(gnorm(G)),
                    rstride=1, cstride=1, linewidth=0, antialiased=True,
                    shade=False, alpha=0.9)

    ax.scatter(*tx_pos, c="black", marker="*", s=260, depthshade=False,
               label="Wi-Fi AP (2.4 GHz, 4x4 array)")
    ax.scatter(rx_pos[:, 0], rx_pos[:, 1], rx_pos[:, 2], c="#111", marker="o",
               s=42, depthshade=False, label="Receivers")

    sm = plt.cm.ScalarMappable(cmap=cmap, norm=norm); sm.set_array([])
    fig.colorbar(sm, ax=ax, shrink=0.5, pad=0.0).set_label("Received power (dBm)")
    sg = plt.cm.ScalarMappable(cmap=plt.cm.turbo, norm=gnorm); sg.set_array([])
    fig.colorbar(sg, ax=ax, shrink=0.5, pad=0.08).set_label("Antenna gain (dBi)")

    ax.set_xlabel("x (m)"); ax.set_ylabel("y (m)"); ax.set_zlabel("z (m)")
    ax.set_title("RFTraceKit — indoor 2.4 GHz Wi-Fi: multipath + AP radiation pattern")
    ax.set_box_aspect((14, 10, 3))
    ax.view_init(elev=26, azim=-62)
    ax.legend(loc="upper left")
    fig.savefig(png_path, dpi=130, bbox_inches="tight")


def render_plotly(meshes, tx_pos, rx_pos, polylines, powers, balloon, html_path):
    import plotly.graph_objects as go

    fig = go.Figure()

    # Walls grouped by material.
    by_mat = {}
    for tris, mat in meshes:
        by_mat.setdefault(mat, []).extend(tris)
    for mat, tris in by_mat.items():
        verts, ijk = [], []
        for t in tris:
            n = len(verts)
            verts += [np.asarray(t.v0), np.asarray(t.v1), np.asarray(t.v2)]
            ijk.append((n, n + 1, n + 2))
        v = np.array(verts)
        fig.add_trace(go.Mesh3d(
            x=v[:, 0], y=v[:, 1], z=v[:, 2],
            i=[a for a, _, _ in ijk], j=[b for _, b, _ in ijk],
            k=[c for _, _, c in ijk], color=MAT_COLOR[mat],
            opacity=0.35 if mat == "glass" else 0.2, name=mat, hoverinfo="skip"))

    # Rays in power bands.
    bins = np.linspace(np.percentile(powers, 5), np.percentile(powers, 95), 7)
    band_col = ["#a50026", "#f46d43", "#fee08b", "#d9ef8b", "#66bd63", "#1a9850"]
    for b in range(6):
        xs, ys, zs = [], [], []
        for pts, pw in zip(polylines, powers):
            if bins[b] <= pw < bins[b + 1] or (b == 5 and pw >= bins[6]):
                for i in range(len(pts) - 1):
                    xs += [pts[i][0], pts[i + 1][0], None]
                    ys += [pts[i][1], pts[i + 1][1], None]
                    zs += [pts[i][2], pts[i + 1][2], None]
        if xs:
            fig.add_trace(go.Scatter3d(x=xs, y=ys, z=zs, mode="lines",
                          line=dict(color=band_col[b], width=2), opacity=0.6,
                          name=f"{bins[b]:.0f}..{bins[b+1]:.0f} dBm"))

    # Radiation-pattern balloon.
    X, Y, Z, G = balloon
    fig.add_trace(go.Surface(x=X, y=Y, z=Z, surfacecolor=G, colorscale="Turbo",
                  opacity=0.95, name="AP pattern",
                  colorbar=dict(title="gain (dBi)", x=1.02, len=0.5)))

    fig.add_trace(go.Scatter3d(x=[tx_pos[0]], y=[tx_pos[1]], z=[tx_pos[2]],
                  mode="markers", marker=dict(size=6, color="black", symbol="diamond"),
                  name="Wi-Fi AP (4x4 array)"))
    fig.add_trace(go.Scatter3d(x=rx_pos[:, 0], y=rx_pos[:, 1], z=rx_pos[:, 2],
                  mode="markers", marker=dict(size=4, color="#111"), name="Receivers"))

    fig.update_layout(
        title="RFTraceKit — indoor 2.4 GHz Wi-Fi: multipath + AP radiation pattern",
        scene=dict(xaxis_title="x (m)", yaxis_title="y (m)", zaxis_title="z (m)",
                   aspectmode="data"))
    fig.write_html(html_path, include_plotlyjs="inline")


if __name__ == "__main__":
    sys.exit(main())
