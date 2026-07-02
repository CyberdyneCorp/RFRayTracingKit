## ADDED Requirements

### Requirement: MSI/PLN antenna pattern import
The library SHALL parse the Planet/MSI `.msi`/`.pln` antenna text format
(`NAME`, `FREQUENCY`, `GAIN`, and `HORIZONTAL <n>` / `VERTICAL <n>` angle-attenuation tables)
into an `AntennaPattern`, normalizing the peak gain to dBi.

#### Scenario: Peak gain and cuts are parsed
- **WHEN** a valid MSI file with GAIN and HORIZONTAL/VERTICAL tables is imported
- **THEN** the resulting `AntennaPattern` SHALL carry the peak gain (in dBi) and both a horizontal
  (azimuth) and a vertical cut populated from the tables

#### Scenario: Malformed MSI file is rejected
- **WHEN** import is given a file that does not contain the expected MSI keywords/tables
- **THEN** the library SHALL report a descriptive error rather than return a partial pattern

### Requirement: AntennaPattern vertical cut and H+V gain
`AntennaPattern` SHALL carry a vertical-cut table (`verticalCutDb`) alongside the existing
azimuth cut, and `gainTowards` SHALL combine the horizontal and vertical cut attenuations with the
peak gain to return a 2D directional gain.

#### Scenario: Boresight returns peak gain
- **WHEN** `gainTowards` is evaluated along the boresight of an MSI-imported pattern
- **THEN** it SHALL return the peak gain (both cuts contribute ~0 dB attenuation at boresight)

#### Scenario: Vertical roll-off is applied off-boresight in elevation
- **WHEN** `gainTowards` is evaluated at an elevation angle with a non-zero vertical-cut
  attenuation
- **THEN** the returned gain SHALL be reduced by the vertical-cut attenuation relative to the peak
  gain

#### Scenario: Omni pattern ignores the vertical cut
- **WHEN** `gainTowards` is evaluated on an omnidirectional pattern with an empty `verticalCutDb`
- **THEN** it SHALL return the peak gain in every direction, unchanged from prior behavior
