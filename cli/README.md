# RFTraceKit CLI tools

Three small front-end executables that expose the load → simulate → export flow
to the shell. They consume only the public API and link `rftrace::rftrace`; there
is no new third-party dependency (argument parsing is the in-tree, header-only
`cli_common.hpp`).

Built behind `RFTRACE_BUILD_CLI` (default `ON`). Configuring with
`-DRFTRACE_BUILD_CLI=OFF` skips `add_subdirectory(cli)` entirely, leaving the
library build and test suite byte-for-byte unchanged. A `just cli` recipe builds
the three targets and smoke-tests `--help`.

Every tool follows the same contract: exit `0` on a clean, complete success;
non-zero with an `error: <msg>` (or `problem: <msg>`) on stderr for any bad
input, unsupported request, or unavailable optional feature — never a crash, a
silent success, or a partial output file.

## rftrace-cli

Load a scene, run a simulation, and write the result.

```
rftrace-cli --tx 0,0,10 --rx 50,0,1.5 --out result.json
rftrace-cli --scene city.obj --tx 5,5,20 --grid -50,-50,5,40,40,1.5 --out cov.csv
```

Key options (`--help` for the full list):

| Flag | Meaning |
|------|---------|
| `--scene <path>` | scene file; optional (empty scene runs LOS-only) |
| `--scene-format <fmt>` | override detection: `mesh\|geojson\|cityjson\|osm\|osmpbf` |
| `--materials <json>` / `--terrain <tif>` | material table / GeoTIFF DEM (terrain needs GDAL) |
| `--tx x,y,z[,freq,power]` | add a transmitter (repeatable) |
| `--rx x,y,z` / `--receivers <file>` | add receivers (repeatable / from a file) |
| `--grid ox,oy,cell,cols,rows,height` | coverage run |
| `--route "x,y,z;..."` `--route-spacing <m>` | drive-test route run |
| `--backend`, `--mode`, `--freq`, `--max-reflections`, `--threads`, `--seed`, `--capture-radius`, `--diffraction [model]`, `--sim-id` | simulation settings |
| `--out <path>` | output; format inferred from the extension |

The run mode is selected by presence of `--grid` (coverage) or `--route`
(route); otherwise a point-receiver run. A receivers file has one `x,y,z` or
`id,x,y,z` per line; `#` and blank lines are skipped.

## rftrace-scene-validator

Load a scene, print a summary (triangle count, bounding box, material count,
tx/rx counts), and report structural problems.

```
rftrace-scene-validator city.obj
rftrace-scene-validator --scene buildings.geojson --scene-format geojson
```

Exits non-zero when the scene is invalid: an empty scene (0 triangles),
degenerate / zero-area triangles (`rawNormal().norm() == 0`), or triangles with
an out-of-range material index. Prints `scene OK` and exits `0` otherwise.

## rftrace-result-converter

Convert a point result written by `rftrace-cli` to another format.

```
rftrace-result-converter --in result.json --out result.csv
rftrace-result-converter --in result.json --out result.geojson
```

Supported conversions: point-result JSON → CSV / GeoJSON / glTF / JSON. The
input kind is sniffed from the JSON text; coverage and route inputs report a
clear "no public loader" error (there is no public loader for those kinds, and
the CLI does not link a JSON library). Unknown kinds or unsupported output
extensions error out non-zero.

## Format inference

Output format is taken from the file extension:

| Extension | Format | Point | Coverage | Route |
|-----------|--------|:-----:|:--------:|:-----:|
| `.json` | JSON | ✅ | ✅ | ✅ |
| `.csv` | CSV | ✅ | ✅ | ✅ |
| `.geojson` | GeoJSON | ✅ | ✅ | — |
| `.gltf` / `.glb` | glTF | ✅ | — | — |
| `.tif` / `.tiff` | GeoTIFF | — | ✅ (GDAL) | — |
| `.parquet` | Parquet | ✅ (Arrow) | — | — |

## Optional features

GeoTIFF output requires a GDAL-enabled build; Parquet output requires an
Arrow/Parquet-enabled build; `.osm.pbf` input requires an osmium-enabled build.
When a requested format needs a feature that was not compiled in, the tool
prints `error: <format> output is not available (built without <dep>)` and exits
non-zero **without** writing a partial file. Availability is probed via the
library's public `io::gdalAvailable()` / `io::parquetAvailable()` /
`io::osmiumAvailable()`.
