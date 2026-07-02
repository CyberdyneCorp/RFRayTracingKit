# wifi_indoor — indoor 2.4 GHz Wi-Fi ray-tracing demo

End-to-end example that runs the whole computation and visualizes it: a detailed
two-room office (concrete shell, glass facade, wood cubicles/desks, a metal
cabinet, a pillar), a ceiling Wi-Fi access point with a **4×4 planar antenna
array steered downward**, stochastic ray-launch multipath (2 bounces), then
exports + renders the result.

## Run

```bash
just py-build          # build the rftracekit._native extension once
PYTHONPATH=bindings/python python3 examples/wifi_indoor/wifi_indoor.py
```

Requires a `python3` with `numpy`, `matplotlib`, and `plotly` (all in a typical
scientific Python env). `pyvista` is optional and not used here.

## Outputs (written next to the script, git-ignored)

| File | What it is |
|------|------------|
| `wifi_indoor.png` | static 3D render (matplotlib) |
| `wifi_indoor.html` | **interactive** 3D scene (Plotly, self-contained) — rotate/zoom |
| `wifi_paths.gltf` | ray paths colored by received power (open in Blender / three.js) |
| `wifi_receivers.geojson` | receiver points + metrics (open in QGIS) |

The visualization shows the material-colored scene, the multipath rays colored by
received power, and the AP's **radiation-pattern balloon** sampled from the same
array-factor gain (`rftracekit.steered_gain_dbi`) that drives the link budget.

Antenna arrays are exposed to Python via `uniform_linear_array` /
`uniform_planar_array`, `steered_gain_dbi`, and `Transmitter.array` /
`beam_steering`.
