## ADDED Requirements

### Requirement: Rain specific attenuation
The library SHALL compute rain specific attenuation γ_R (dB/km) from rain rate R (mm/h) and
frequency using the ITU-R P.838 power law γ_R = k·R^α, with k and α interpolated for the
link frequency and polarization.

#### Scenario: Attenuation scales with rain rate
- **WHEN** the rain rate increases at a fixed mmWave frequency
- **THEN** the specific attenuation SHALL increase according to the P.838 power law

#### Scenario: Negligible at low frequency
- **WHEN** rain attenuation is evaluated below ~5 GHz
- **THEN** the specific attenuation SHALL be near zero (rain matters mainly at mmWave)

### Requirement: Gaseous atmospheric attenuation
The library SHALL apply an approximate gaseous (oxygen/water-vapour) specific attenuation
(ITU-R P.676 style) as a function of frequency, defaulting to a documented standard
atmosphere.

#### Scenario: Applied over path length
- **WHEN** a path of length L km is evaluated with atmospheric attenuation enabled
- **THEN** the path's received power SHALL be reduced by (γ_gas)·L in addition to other losses

### Requirement: Atmospheric loss in the path budget
The library SHALL add rain and gaseous attenuation, when enabled via settings, to a path's
total loss.

#### Scenario: Disabled by default preserves prior results
- **WHEN** atmospheric attenuation is disabled
- **THEN** per-path received power SHALL match the Phase 1/2 budget exactly
