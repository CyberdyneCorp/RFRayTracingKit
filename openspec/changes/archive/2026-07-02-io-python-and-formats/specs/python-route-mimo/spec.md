## ADDED Requirements

### Requirement: Python Route binding
The Python module SHALL expose a `Route` type with an ordered list of `waypoints`, a
`sample_spacing`, and an optional `speed`, mapping to the C++ `Route` (`waypoints`,
`sampleSpacing`, `speedMps`).

#### Scenario: Route constructed from Python
- **WHEN** a `Route` is constructed with waypoints and a sample spacing from Python
- **THEN** the object SHALL expose those waypoints and spacing back to Python and be accepted by
  `Simulator.run_route`

### Requirement: Python route simulation binding
The Python `Simulator` SHALL expose `run_route(scene, route)` returning a Python `RouteResult`
wrapper whose ordered `RouteSample` series carries each sample's `position`, RF metrics, and
`doppler_hz`, delegating to the C++ `Simulator::runRoute`.

#### Scenario: Drive-test route simulated from Python
- **WHEN** `simulator.run_route(scene, route)` is called from Python
- **THEN** it SHALL return a `RouteResult` whose samples are ordered along the route and expose
  `position`, received power / path loss / delay-spread metrics, and `doppler_hz`

#### Scenario: Doppler reported per sample
- **WHEN** a `Route` with a positive `speed` is simulated
- **THEN** each `RouteSample` SHALL expose a `doppler_hz` value derived from the C++ per-sample
  Doppler

### Requirement: Python MIMO channel matrix binding
The Python module SHALL expose `rf.mimo.channel_matrix(receiver_result, tx_array, rx_array)`
returning the narrowband channel matrix `H` as a NumPy complex 2D array of shape
`(n_rx, n_tx)`, delegating to the C++ `rf::channelMatrix`.

#### Scenario: Channel matrix returned as a NumPy complex array
- **WHEN** `rf.mimo.channel_matrix(receiver_result, tx_array, rx_array)` is called
- **THEN** it SHALL return a NumPy array with complex dtype and shape `(rx_array size, tx_array
  size)` matching the C++ `channelMatrix`

### Requirement: Python MIMO capacity and per-stream SINR bindings
The Python module SHALL expose `rf.mimo.capacity(H, snr_linear)` returning the narrowband capacity
in bits/s/Hz and `rf.mimo.per_stream_sinr(H, snr_linear)` returning the descending per-stream
SINRs, delegating to the C++ `rf::capacity` and `rf::perStreamSinr`.

#### Scenario: Capacity computed from a channel matrix in Python
- **WHEN** `rf.mimo.capacity(H, snr_linear)` is called with a NumPy complex `H` and a positive
  linear SNR
- **THEN** it SHALL return the log-det capacity in bits/s/Hz equal to the C++ `capacity`

#### Scenario: Per-stream SINRs returned in descending order
- **WHEN** `rf.mimo.per_stream_sinr(H, snr_linear)` is called
- **THEN** it SHALL return the per-stream SINRs in descending order equal to the C++
  `perStreamSinr`

### Requirement: Python MIMO JSON export binding
The Python `Result` wrapper (or `rf.io`) SHALL expose `to_mimo_json` producing the MIMO JSON
string of the C++ `io::mimoToJsonString`.

#### Scenario: MIMO summary exported to JSON from Python
- **WHEN** `result.to_mimo_json(...)` is called from Python
- **THEN** it SHALL return a JSON string equivalent to the C++ `mimoToJsonString` output
