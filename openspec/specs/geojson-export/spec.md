# geojson-export Specification

## Purpose
TBD - created by archiving change phase2-rf-multipath. Update Purpose after archive.
## Requirements
### Requirement: GeoJSON receiver export
The library SHALL export receivers as a GeoJSON `FeatureCollection` of Point features whose
properties include received power, path loss, and delay spread.

#### Scenario: Receivers become point features
- **WHEN** a result is exported to receiver GeoJSON
- **THEN** each receiver SHALL be a Point Feature with `received_power_dbm`, `path_loss_db`,
  and `delay_spread_ns` in its properties

### Requirement: GeoJSON ray-path export
The library SHALL export ray paths as a GeoJSON `FeatureCollection` of LineString features
whose properties include the path type, power, and reflection count.

#### Scenario: Paths become line features
- **WHEN** a result is exported to path GeoJSON
- **THEN** each path SHALL be a LineString Feature whose coordinates are the path points and
  whose properties include `type`, `power_dbm`, and `reflections`

### Requirement: GeoJSON coverage export
The library SHALL export a coverage grid as GeoJSON polygon cells (or points) carrying the
per-cell power value.

#### Scenario: Coverage cells become features
- **WHEN** a coverage result is exported to GeoJSON
- **THEN** each covered cell SHALL be a Feature with its `received_power_dbm` value, and
  no-signal cells SHALL be omitted or flagged per a documented rule

### Requirement: Valid GeoJSON structure
Exported GeoJSON SHALL be valid: a top-level `FeatureCollection` with a `features` array,
each feature having `type`, `geometry`, and `properties`.

#### Scenario: Output parses as GeoJSON
- **WHEN** any GeoJSON export is re-parsed
- **THEN** it SHALL be a `FeatureCollection` and every feature SHALL have a `geometry` with
  valid `type` and `coordinates`

