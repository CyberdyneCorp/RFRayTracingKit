## 1. Scaffolding & shared helpers

- [ ] 1.1 Add `cli/` with `cli_common.hpp`: a minimal in-tree arg parser (`--key value`/`--key=value`/`--flag`, `-h/--help`, `--version`) with typed getters + clear-error throwing, and `formatFromPath()` (extension → format enum) + a scene-format detector with a `--scene-format` override.
- [ ] 1.2 CMake: `option(RFTRACE_BUILD_CLI "Build the CLI tools" ON)`; `add_subdirectory(cli)` guarded by it; three executables linking `rftrace::rftrace`. Add a `just cli` recipe.
- [ ] 1.3 Unit-test the arg parser and `formatFromPath` (valid/invalid args, each extension, unknown extension).

## 2. rftrace-cli

- [ ] 2.1 Scene loading: dispatch on scene format (mesh via `importMesh`; GeoJSON/CityJSON/OSM), optional `--materials` (`importMaterials`) and `--terrain`.
- [ ] 2.2 Transmitters/receivers: `--tx x,y,z[,freq,power]` (repeatable), `--rx x,y,z` and/or `--receivers file`; coverage `--grid ox,oy,cell,cols,rows,height`; route `--route` waypoints + spacing.
- [ ] 2.3 Settings from flags: `--backend`, `--mode image|raylaunch`, `--freq`, `--max-reflections`, `--threads`, `--diffraction`, etc. (via `backendFromString` / enum parsers).
- [ ] 2.4 Run the selected mode (`run`/`runCoverage`/`runRoute`) and write `--out` via the exporter matching the result kind + inferred format; `--help`/`--version`; exit 0 on success.
- [ ] 2.5 Optional-feature awareness: requesting a GeoTIFF/Parquet output without the feature compiled in → clear error + non-zero exit (no partial file).

## 3. scene-validator & result-converter

- [ ] 3.1 `rftrace-scene-validator`: load a scene, print a summary (triangles, bounds, materials, tx/rx counts), detect problems (degenerate/zero-area triangles, missing materials, empty scene), exit non-zero when invalid.
- [ ] 3.2 `rftrace-result-converter`: read an `rftrace-cli` result file, infer kind (point/coverage/route) + output format, convert to another supported format (e.g. JSON→CSV/GeoJSON), clear error on unsupported conversions.

## 4. Tests, docs & archive

- [ ] 4.1 Integration tests (via CTest): `rftrace-cli` on a tiny mesh + receivers → JSON and coverage → CSV (assert exit 0 + output content); missing-scene and unavailable-format error paths (non-zero exit); validator on a valid and a degenerate scene; converter JSON→CSV.
- [ ] 4.2 Confirm `RFTRACE_BUILD_CLI=OFF` leaves the build + test suite unchanged.
- [ ] 4.3 Add a `cli/README.md` (usage for each tool) and a README section; update `openspec/project.md` (CLI tools moved from not-built to done). Run `openspec validate --strict` and archive the change.
