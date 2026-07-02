# dev-tooling Specification

## Purpose
TBD - created by archiving change phase1-cpu-prototype. Update Purpose after archive.
## Requirements
### Requirement: just task runner
The project SHALL provide a `justfile` at the repository root as the canonical developer
task runner, following the CyberdyneCorp convention (as in the NumPP and SciPP projects),
so build, test, and validation workflows are invoked the same way across projects.

#### Scenario: Listing recipes
- **WHEN** a developer runs `just` with no arguments (or `just --list`)
- **THEN** the default recipe SHALL print the available recipes and their descriptions

#### Scenario: Recipe names are consistent with sibling projects
- **WHEN** the justfile is compared to the NumPP/SciPP convention
- **THEN** it SHALL provide the shared recipe surface `configure`, `build`, `lib`, `test`
  (with a `unit` alias), `ctest`, `gcc`, `asan`, `spec`, `ci`, `install`, and `clean`

### Requirement: Build and test recipes
The justfile SHALL expose recipes to configure, build, and test the library using the
project's CMake + vcpkg toolchain, and SHALL build examples so they can be run.

#### Scenario: Configure passes through the vcpkg toolchain and extra flags
- **WHEN** a developer runs `just configure -DRFTRACE_ENABLE_EMBREE=ON`
- **THEN** CMake SHALL be invoked with the vcpkg toolchain file and the extra flag forwarded

#### Scenario: Build then test
- **WHEN** a developer runs `just test`
- **THEN** the library and test suite SHALL be built and the GoogleTest binary executed

#### Scenario: Run a single example
- **WHEN** a developer runs `just example simple_los`
- **THEN** the corresponding example binary SHALL be built and executed

### Requirement: OpenSpec validation recipe
The justfile SHALL provide a `spec` recipe that validates all OpenSpec specs and changes.

#### Scenario: Validate specs
- **WHEN** a developer runs `just spec`
- **THEN** it SHALL run `openspec validate --all --strict` and fail non-zero if any spec or
  change is invalid

### Requirement: Aggregate local CI recipe
The justfile SHALL provide a `ci` recipe that runs the full local verification gate.

#### Scenario: Local CI gate
- **WHEN** a developer runs `just ci`
- **THEN** it SHALL run the test suite, a second-compiler (gcc) build+test, the sanitizer
  build+test, OpenSpec validation, and the examples, failing if any step fails

### Requirement: Sanitizer build recipe
The justfile SHALL provide an `asan` recipe that builds and tests under AddressSanitizer and
UndefinedBehaviorSanitizer in a separate build directory.

#### Scenario: Sanitized test run
- **WHEN** a developer runs `just asan`
- **THEN** a separate ASan/UBSan build SHALL be produced and its test suite executed

