## ADDED Requirements

### Requirement: Terrain/building diffraction profile
The library SHALL represent a diffraction path as an ordered profile of
`(distance-from-transmitter, obstacle-top-height)` samples between a transmitter and a
receiver, with the transmitter and receiver antenna heights as endpoints, over which
multi-obstacle diffraction loss is computed.

#### Scenario: Profile is built from ordered samples
- **WHEN** a shadowed transmitter–receiver link is analysed with terrain and building tops
  sampled along the great-circle/straight span
- **THEN** the profile SHALL be an ordered sequence of `(distance, top-height)` samples with the
  transmitter at distance 0 and the receiver at the total span, and diffraction loss SHALL be
  computed from that profile

### Requirement: Bullington equivalent-edge diffraction
The library SHALL compute multi-obstacle diffraction loss by the Bullington method: construct
an equivalent single knife-edge from the steepest transmitter-side and receiver-side slopes of
the profile, and apply the single knife-edge loss at that equivalent edge.

#### Scenario: Bullington reference value
- **WHEN** Bullington loss is computed for a profile with two obstacles of known geometry
- **THEN** the loss SHALL equal the knife-edge loss of the equivalent Bullington edge within a
  documented tolerance

### Requirement: Deygout recursive diffraction
The library SHALL compute multi-obstacle diffraction loss by the Deygout method: identify the
dominant edge (largest Fresnel parameter v) over the full span, add its knife-edge loss, recurse
ONCE into the transmitter-side sub-span and ONCE into the receiver-side sub-span, and apply the
standard Deygout clearance correction to the summed result.

#### Scenario: Deygout reference value
- **WHEN** Deygout loss is computed for a profile with two obstacles of known geometry
- **THEN** the total loss SHALL equal the sum of the dominant-edge and sub-span knife-edge losses
  with the standard clearance correction, within a documented tolerance

#### Scenario: Adding an obstacle increases loss
- **WHEN** a second obstacle is added to a single-obstacle profile
- **THEN** both the Bullington and Deygout diffraction losses SHALL be greater than or equal to
  the single-obstacle loss

### Requirement: Single-edge reduction
Both the Bullington and Deygout methods SHALL reduce exactly to the existing single knife-edge
loss `knifeEdgeLossDb(fresnelDiffractionParameter(...))` for a profile that presents a single
diffracting obstacle (one interior obstacle above the line of sight, all other samples below it),
preserving archived single-edge behavior.

#### Scenario: One obstacle reduces to knife-edge
- **WHEN** multi-edge loss is computed for a profile whose only above-LOS sample is a single
  obstacle with parameters (h, d1, d2, lambda)
- **THEN** the result SHALL equal `knifeEdgeLossDb(fresnelDiffractionParameter(h, d1, d2, lambda))`
  within a documented tolerance (bit-for-bit consistent with the archived single-edge path)

#### Scenario: Fully cleared profile
- **WHEN** every profile sample lies below the transmitter–receiver line of sight
- **THEN** both methods SHALL return approximately 0 dB of diffraction loss
