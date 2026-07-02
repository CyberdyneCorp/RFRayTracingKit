## Why

The engine so far models line-of-sight and specular multipath with free-space, reflection,
and penetration losses. Real 4G/5G/6G planning needs the effects that dominate at building
edges, over long/high-frequency links, through foliage, and across multi-cell networks:
diffraction around obstacles, rain/atmospheric and vegetation attenuation, antenna-array
beamforming, MIMO channel capacity, SINR/serving-cell analysis, and drive-test (moving
receiver) simulation. Phase 7 adds these on the CPU backend, each validated against an
analytic or published-model reference, keeping the CPU engine the correctness oracle for
future GPU acceleration.

## What Changes

- **Diffraction**: knife-edge / multiple-edge diffraction (ITU-R P.526 style) producing
  diffracted paths over obstacle edges, gated by `SimulationSettings.enableDiffraction`.
- **Atmospheric attenuation**: specific attenuation from rain (ITU-R P.838) and gaseous
  absorption (ITU-R P.676 approximation), applied per path length and frequency.
- **Vegetation attenuation**: foliage loss (Weissberger / ITU-R P.833) when a path traverses
  a vegetation-tagged volume or material.
- **Antenna arrays**: array geometry (element positions), per-element excitation/phase, beam
  steering, and array-factor gain feeding the existing antenna-gain link-budget term.
- **MIMO channel**: per-path direction-of-departure/arrival, phase and polarization combined
  into a channel matrix H, with a capacity (and per-stream SINR) estimate.
- **Cell planning / SINR**: aggregate multiple transmitters into serving-cell selection,
  interference, and an SINR figure per receiver and per coverage cell.
- **Route simulation**: a moving receiver along an ordered set of waypoints producing a
  position/time series of results (drive-test style).
- Wire the new loss terms into the per-path budget (`rf-propagation`) and the new path/mode
  behavior into `ray-simulation`; extend results (`results-export`) with SINR, MIMO, and
  route series.

## Capabilities

### New Capabilities
- `diffraction`: knife-edge/multiple-edge diffraction physics and diffracted-path finding.
- `atmospheric-attenuation`: rain + gaseous specific attenuation applied to paths.
- `vegetation-attenuation`: foliage loss along paths crossing vegetation.
- `antenna-arrays`: array geometry, beam steering, and array-factor gain.
- `mimo-channel`: MIMO channel matrix and capacity/SINR estimation.
- `cell-planning`: multi-transmitter SINR, serving-cell selection, and SINR coverage.
- `route-simulation`: moving-receiver route mode producing a result series.

### Modified Capabilities
- `ray-simulation`: add diffraction path finding (behind `enableDiffraction`) and the route
  simulation mode.
- `rf-propagation`: add the atmospheric/vegetation/diffraction loss terms to the per-path
  received-power budget.
- `results-export`: add SINR fields, the MIMO channel matrix, and route-series export.

## Impact

- **Code (new):** `src/rf/diffraction.*` (already a stub header name), `src/rf/atmospheric.*`,
  `src/rf/vegetation.*`, `src/rf/array.*`, `src/rf/mimo.*`, `src/core/cell_planning.*`,
  `src/core/route.*`; extends `SimulationSettings`, `Simulator`, `RFResult`.
- **Dependencies:** none new (Eigen covers array/MIMO linear algebra).
- **Out of scope:** GPU implementations of these models (a later cross-backend task),
  terrain/GeoTIFF ingestion, full ray-optical UTD, 3D-Tiles/CZML animation of routes.
- **Note:** this is a large phase; see design.md — implementation is expected to proceed as
  several smaller sub-changes per capability rather than one monolithic apply.
