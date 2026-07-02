## ADDED Requirements

### Requirement: SINR from multiple transmitters
The library SHALL compute, per receiver, the SINR from multiple transmitters by treating the
serving transmitter's received power as signal, the other transmitters' received power as
interference, plus a thermal noise floor. The noise floor SHALL default to the physically
derived value N = kTB + NF, where k is Boltzmann's constant, T = 290 K, B is a configurable
bandwidth, and NF is a configurable receiver noise figure (dB); a fixed-dBm override MAY be
provided.

#### Scenario: SINR combines signal, interference and noise
- **WHEN** a receiver is reached by a serving transmitter and one interferer
- **THEN** SINR SHALL equal serving_power / (interferer_power + noise) in linear units,
  reported in dB

#### Scenario: Noise floor derived from bandwidth and noise figure
- **WHEN** the noise floor is computed for bandwidth B and noise figure NF at T = 290 K
- **THEN** it SHALL equal 10·log10(k·T·B / 1mW) + NF dBm within a documented tolerance
  (e.g. ≈ −174 dBm/Hz + 10·log10(B) + NF)

#### Scenario: Single transmitter reduces to SNR
- **WHEN** only one transmitter reaches a receiver
- **THEN** the interference term SHALL be zero and SINR SHALL equal SNR

### Requirement: Serving-cell selection
The library SHALL select each receiver's serving transmitter as the one with the highest
received power (configurable to a best-server rule).

#### Scenario: Strongest transmitter serves
- **WHEN** two transmitters reach a receiver with different received powers
- **THEN** the higher-power transmitter SHALL be recorded as the serving cell

### Requirement: SINR coverage map
The library SHALL produce an SINR value per coverage-grid cell using the multi-transmitter
SINR model, reusing the coverage-grid mode.

#### Scenario: SINR coverage array
- **WHEN** a coverage grid is simulated with multiple transmitters and SINR enabled
- **THEN** the result SHALL expose an SINR array aligned to the grid, with a no-signal sentinel
  where no transmitter is received
