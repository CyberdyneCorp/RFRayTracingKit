## ADDED Requirements

### Requirement: Multipath coverage mode
The library SHALL provide a coverage mode that computes received power per grid cell from
multipath — specular reflections, and diffraction when enabled — rather than line-of-sight plus
free-space path loss only, implemented via the ray-launch engine.

#### Scenario: Reflection contributes to a cell
- **WHEN** a grid cell is reached by a strong specular reflection in addition to (or instead of)
  the direct path in multipath coverage mode
- **THEN** the cell's received power SHALL reflect the accumulated multipath contribution and
  SHALL exceed the power of a LOS + FSPL-only estimate for that cell

### Requirement: Ray-launch cell capture and accumulation
In multipath coverage mode the library SHALL treat each grid cell as a capture point with a
capture radius of approximately `cellSize/2` at the cell's terrain height, launch rays ONCE per
transmitter (reusing the ray-launch engine), and accumulate captured multipath power per cell
incoherently (power sum).

#### Scenario: Rays launched once per transmitter
- **WHEN** multipath coverage is run for a scene with one transmitter
- **THEN** rays SHALL be launched once for that transmitter and captured contributions SHALL be
  accumulated across all cells, rather than re-launching per cell

#### Scenario: Incoherent accumulation per cell
- **WHEN** multiple captured paths reach the same cell
- **THEN** the cell power SHALL be the incoherent sum `10·log10(Σ 10^(Pi/10))` of the captured
  per-path powers

### Requirement: Deterministic coverage remains the default
The existing deterministic per-cell coverage (LOS + image method) SHALL remain the default; the
ray-launch multipath coverage SHALL be selected when `SimulationSettings.mode == RayLaunch`, so
default coverage results are unchanged.

#### Scenario: Default mode reproduces archived coverage
- **WHEN** coverage is run with the default mode (image method)
- **THEN** the coverage array SHALL be identical to the archived per-cell LOS + image result
  bit-for-bit

#### Scenario: Ray-launch coverage selected by mode
- **WHEN** `SimulationSettings.mode` is `RayLaunch`
- **THEN** coverage SHALL be computed via the ray-launch multipath engine

### Requirement: Shadowed-cell diffraction fill
In multipath coverage mode, fully-shadowed cells SHALL retain the no-signal sentinel unless
diffraction is enabled, in which case they MAY be filled using the per-cell knife-edge or
multi-edge diffraction path.

#### Scenario: Shadowed cell without diffraction keeps the sentinel
- **WHEN** a cell is reached by no captured ray and diffraction is disabled
- **THEN** its coverage value SHALL be the documented no-signal sentinel
