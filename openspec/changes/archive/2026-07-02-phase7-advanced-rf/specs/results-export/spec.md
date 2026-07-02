## ADDED Requirements

### Requirement: SINR in receiver results
Per-receiver results SHALL, when SINR is enabled, include the serving transmitter id, the SINR
(dB), and the interference power, in addition to the existing metrics.

#### Scenario: Serving cell and SINR present
- **WHEN** a multi-transmitter simulation with SINR enabled completes
- **THEN** each reached receiver's result SHALL carry a serving transmitter id and an SINR value

### Requirement: MIMO and route result export
The library SHALL export the MIMO channel matrix (and capacity) for an array pair, and export a
route result series (JSON and CSV) in sample order.

#### Scenario: Route CSV in order
- **WHEN** a route result is exported to CSV
- **THEN** rows SHALL appear in route-sample order with position and metrics per row

#### Scenario: MIMO matrix export
- **WHEN** a MIMO result is exported
- **THEN** the output SHALL contain the complex channel matrix dimensions and entries plus the
  capacity estimate
