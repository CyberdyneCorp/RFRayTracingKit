## ADDED Requirements

### Requirement: MIMO channel matrix
The library SHALL assemble a MIMO channel matrix H (n_rx × n_tx complex) for a transmitter/
receiver array pair from the per-path complex gains, directions of departure/arrival, and
per-element array responses.

#### Scenario: Matrix dimensions match arrays
- **WHEN** a transmitter array of M elements and a receiver array of N elements are simulated
- **THEN** the channel matrix H SHALL have shape N × M with complex entries

#### Scenario: Single-path rank
- **WHEN** exactly one propagation path links the arrays
- **THEN** H SHALL be (approximately) rank 1

### Requirement: Capacity and stream SINR estimate
The library SHALL compute an estimated MIMO capacity (bits/s/Hz) from H at a given SNR using
the log-det water-filling/equal-power formula, and expose per-stream SINR.

#### Scenario: Capacity grows with spatial streams
- **WHEN** a rich multipath channel (well-conditioned H) is evaluated versus a rank-1 channel
  at the same total SNR
- **THEN** the multipath channel SHALL report higher capacity than the rank-1 channel
