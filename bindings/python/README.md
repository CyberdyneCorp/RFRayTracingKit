# rftracekit

Python bindings for the **RFTraceKit** C++ RF ray-tracing / propagation engine.

```python
import rftracekit as rf

scene = rf.Scene()
scene.add_transmitter(id="tx", position=[0, 0, 30], frequency_hz=3.5e9, power_dbm=43)
scene.add_receiver(id="rx", position=[100, 0, 1.5])

result = rf.Simulator(rf.SimulationSettings(mode="raylaunch")).run(scene)
print(result.received_power_dbm)          # numpy array
df = result.receivers_dataframe()         # pandas (optional)
result.to_json("out.json")
```

## Layout

- `rftracekit/` — pure-Python wrappers around the compiled `rftracekit._native`
  extension (`scene`, `simulator`, `results`, `materials`, `antennas`).
- `rftracekit/io/` — wrapper-aware helpers over the native JSON/CSV/GeoJSON/glTF
  exporters.
- `rftracekit/viz/` — optional plotting (`pyvista`, `plotly`), imported lazily.

## Optional extras

- `pip install rftracekit[viz]` — pyvista + plotly plotting.
- `pip install rftracekit[data]` — pandas dataframes.

Both are imported lazily; importing `rftracekit` never requires them.

## Development

The extension is built by the main CMake with `-DRFTRACE_ENABLE_PYTHON=ON` into
`rftracekit/`. Run the tests against the build tree with:

```sh
just py-build
just py-test
```
