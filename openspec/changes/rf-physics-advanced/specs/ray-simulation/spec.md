## ADDED Requirements

### Requirement: Diffraction model selection
`SimulationSettings` SHALL let a caller select the diffraction model used for shadowed links —
the single knife-edge model or a multi-edge model (Bullington / Deygout over a profile) — with
the single knife-edge model as the default so archived diffraction results are unchanged.
(The UTD wedge coefficient is provided as a validated primitive in `rf/utd.hpp`; exposing it as a
third selectable path model is a documented follow-up — see the utd-diffraction spec.)

#### Scenario: Knife-edge is the default model
- **WHEN** `SimulationSettings` is default-constructed
- **THEN** the diffraction model SHALL be the knife-edge model and diffracted-path loss SHALL
  match the archived single knife-edge behavior

#### Scenario: Multi-edge model over a multi-obstacle profile
- **WHEN** the multi-edge (Bullington/Deygout) model is selected and a shadowed link crosses
  several obstacles
- **THEN** the diffracted path SHALL use the multi-edge profile loss, reducing exactly to the
  single knife-edge loss when only one obstacle diffracts

### Requirement: Multipath coverage mode selection
The coverage simulation SHALL support a multipath mode that includes specular reflections (and
diffraction when enabled) via the ray-launch engine, selected when `SimulationSettings.mode ==
RayLaunch`, while the deterministic LOS + image-method per-cell coverage remains the default.

#### Scenario: Default coverage is LOS + image method
- **WHEN** coverage is run with the default mode (image method)
- **THEN** the coverage result SHALL be the archived deterministic per-cell LOS + image output,
  bit-for-bit

#### Scenario: Ray-launch multipath coverage
- **WHEN** `SimulationSettings.mode` is `RayLaunch` for a coverage run
- **THEN** the simulator SHALL launch rays once per transmitter, treat each cell as a capture
  point at its terrain height, and accumulate captured multipath power per cell
