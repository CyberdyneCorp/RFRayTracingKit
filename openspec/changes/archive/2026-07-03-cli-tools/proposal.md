## Why

The engine is a library (C++, Python, C/Swift) with example programs, but there is no way to run a
simulation from the shell without writing code. A small set of command-line tools makes the library
usable in scripts, pipelines, and quick experiments: load a scene file, run a simulation, and write
results — plus validate scenes and convert result formats. All the pieces already exist (importers,
`Simulator`, exporters); the CLIs just wire them behind argument parsing.

## What Changes

- **`rftrace-cli`** — the main driver. Load a scene from a file (mesh OBJ/glTF via `importMesh`,
  GeoJSON/CityJSON/OSM, optional `--materials` and `--terrain`), add transmitters/receivers (from
  flags and/or a receivers file), configure the simulation (backend, mode, frequency, reflections,
  threads, diffraction, …), run `run`/`runCoverage`/`runRoute`, and write results to an output whose
  format is inferred from its extension (`.json`, `.csv`, `.geojson`, `.gltf`, `.tif`). Unknown or
  unavailable formats (e.g. GeoTIFF without GDAL) produce a clear error.
- **`rftrace-scene-validator`** — load a scene and report a summary (triangle count, bounds, material
  count, transmitter/receiver counts) and any problems (degenerate/zero-area triangles, missing
  materials, empty scene); exit non-zero when the scene is invalid.
- **`rftrace-result-converter`** — read a result file (an `rftrace-cli` JSON result) and convert it to
  another supported format (CSV/GeoJSON/glTF for point results; CSV/GeoJSON/GeoTIFF for coverage),
  inferring input/output kind and format.
- **Build & tests**: the tools build behind `RFTRACE_BUILD_CLI` (default ON, mirroring
  `RFTRACE_BUILD_EXAMPLES`), depending only on the core library; each has `--help`/`--version`.
  Integration tests invoke the built binaries on tiny fixtures and assert exit codes and outputs.

## Capabilities

### New Capabilities
- `cli-tools`: command-line front-ends over the existing importers/simulator/exporters —
  `rftrace-cli` (load → simulate → export with extension-inferred formats and clear errors),
  `rftrace-scene-validator` (scene summary + validation with a non-zero exit on invalid input), and
  `rftrace-result-converter` (result-format conversion) — with argument parsing, `--help`/`--version`,
  and integration tests.

### Modified Capabilities
<!-- None. The CLIs only consume the existing public library API; no library behaviour changes. -->

## Impact

- **Code**: new `cli/` directory (one `main.cpp` per tool + a small shared arg-parsing/format helper),
  wired via `add_subdirectory(cli)` behind `RFTRACE_BUILD_CLI` in CMake; a `just cli` recipe.
- **Public API**: none changed — the tools are thin front-ends over existing headers.
- **Deps**: no new required dependency; argument parsing is hand-rolled (header-only, no third-party).
  Formats that need optional features (GeoTIFF→GDAL, Parquet→Arrow) are probed and error clearly when
  the feature is absent.
- **Tests**: CLI integration tests (invoke the binaries on fixtures: a tiny mesh + a receivers file →
  run → assert JSON/CSV output and exit codes; validator on a good and a degenerate scene; converter
  JSON→CSV). Unit tests for the arg parser and format-from-extension inference.
- **Risk**: low — no library change; the surface is argument handling and file I/O, covered by the
  integration tests. The default build gains three small executables (gated by the flag).
