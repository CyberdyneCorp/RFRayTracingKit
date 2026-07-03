## Context

All building blocks exist: importers (`importMesh` for OBJ/glTF via assimp, `importGeoJSON`,
`importCityJSON`, `importOSM`, `importMaterials`, GeoTIFF terrain), `Simulator`
(`run`/`runCoverage`/`runRoute`) + `SimulationSettings` (with `backendFromString`, `PropagationMode`,
`DiffractionModel`), and exporters (`exportResultJson`, `exportReceiversCsv`/`exportCoverageCsv`,
`exportReceiversGeoJson`/`exportPathsGeoJson`/`exportCoverageGeoJson`, `exportPathsGltf`,
`exportCoverageGeoTiff` (GDAL), `exportReceiversParquet` (Arrow), `exportRouteJson`). The examples
already demonstrate the load→simulate→export flow in code; the CLIs expose it to the shell. No new
library code is needed — only front-ends, arg parsing, and format dispatch.

## Goals / Non-Goals

**Goals:**
- Three small, well-behaved executables (`rftrace-cli`, `rftrace-scene-validator`,
  `rftrace-result-converter`) with `--help`/`--version`, clear errors, and correct exit codes.
- Scene format auto-detected from input; output format inferred from extension.
- Build behind `RFTRACE_BUILD_CLI` (default ON), no new dependency, default build otherwise unchanged.
- Integration + unit tests.

**Non-Goals:**
- No new importers/exporters or simulation features; no scripting/config-file DSL (flags + a simple
  receivers file suffice in v1); no interactive TUI.
- Not re-exposing every advanced-RF knob in v1 — the common ones (backend, mode, frequency, power,
  reflections, threads, diffraction) plus additive flags as needed.

## Decisions

### D1 — Layout: `cli/` dir, one main per tool, shared header
`cli/rftrace_cli.cpp`, `cli/scene_validator.cpp`, `cli/result_converter.cpp`, plus
`cli/cli_common.hpp` (arg parsing + format-from-extension + error helpers). Built via
`add_subdirectory(cli)` behind `RFTRACE_BUILD_CLI` (default ON), mirroring `RFTRACE_BUILD_EXAMPLES`.
Executables named `rftrace-cli`, `rftrace-scene-validator`, `rftrace-result-converter`. *Why:* mirrors
the examples pattern; one flag gates the lot.

### D2 — Argument parsing: hand-rolled, in-tree (no dependency)
A tiny header-only parser: `--key value` / `--flag` / `--key=value`, `-h/--help`, `--version`, with
typed getters (string/double/int/bool) and required/default handling that throws a clear message on
error. *Why:* avoids adding a third-party arg library for a small surface; keeps the default build
dependency-free. *Alternative considered:* CLI11/cxxopts via vcpkg — rejected for v1 to avoid a new
dep; can adopt later if the surface grows.

### D3 — Format dispatch by file extension
A `formatFromPath(path)` maps `.json/.csv/.geojson/.gltf/.tif` to an enum; the CLI dispatches to the
matching exporter based on the result kind (point vs coverage vs route). Scene input format is detected
by extension too (`.obj/.gltf/.glb` → mesh; `.geojson` → GeoJSON; `.city.json/.json` → CityJSON/OSM as
specified by a `--scene-format` override when ambiguous). *Why:* ergonomic and unambiguous for the
common cases, with an explicit override escape hatch.

### D4 — Optional-feature awareness
GeoTIFF export requires GDAL (`RFTRACE_ENABLE_GDAL`), Parquet requires Arrow. The CLI probes
availability (compile guards / the exporters' `*_available()` where present) and, when a requested
format needs an absent feature, prints a clear "feature not compiled in" error and exits non-zero —
never a partial/empty file. *Why:* the spec's unavailable-format requirement; predictable behaviour.

### D5 — Transmitters/receivers input
Transmitters and receivers via repeatable flags (`--tx x,y,z[,freq,power]`, `--rx x,y,z`) and/or a
simple whitespace/CSV receivers file (`--receivers rx.csv`). Coverage mode takes grid flags
(`--grid ox,oy,cell,cols,rows,height`). Route mode takes `--route x,y,z;...` waypoints + spacing.
*Why:* covers the three simulation modes without a config-file format in v1.

### D6 — Tests: integration (invoke the binary) + unit (parser/format)
Integration tests run the built executables via `ctest`/a small harness on tiny fixtures (a 2-triangle
mesh, a receivers file) and assert exit codes and that the output file exists and contains expected
content; validator tests cover a valid and a degenerate scene; converter tests cover JSON→CSV. Unit
tests cover the arg parser and `formatFromPath`. *Why:* the risk is in argument handling and dispatch,
which end-to-end invocation exercises directly.

## Risks / Trade-offs

- **[Hand-rolled parser edge cases]** → keep the grammar minimal and covered by unit tests; clear
  errors on unknown/missing args.
- **[Ambiguous scene format by extension]** → a `--scene-format` override; document the default mapping.
- **[Optional-feature output requested but not built]** → probe + clear error (D4), tested.
- **[Fixture/test portability]** → use in-tree tiny fixtures and temp output paths; no network/large data.
- **[Default build gains executables]** → gated by `RFTRACE_BUILD_CLI`; flag-off build is unchanged
  (tested).

## Migration Plan

- PR: add `cli/` (three tools + shared header), CMake `RFTRACE_BUILD_CLI` + `add_subdirectory(cli)`,
  a `just cli` recipe, and the integration/unit tests. Fully buildable/testable here.
- Follow-up (optional): add CLI to the CI matrix; a config-file input; more advanced-RF flags.

## Open Questions

- Config-file input (JSON) vs flags-only for v1 — start flags + a receivers file; add a config file
  later if needed.
- Whether to adopt a vcpkg arg-parsing library later (CLI11) if the option surface grows — defer.
