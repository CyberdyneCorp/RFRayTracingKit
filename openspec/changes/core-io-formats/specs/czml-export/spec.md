## ADDED Requirements

### Requirement: CZML result export
The library SHALL export an RF result to a Cesium CZML JSON document consisting of a document
packet, one point packet per receiver, and one polyline packet per ray path.

#### Scenario: Packet count matches the result
- **WHEN** a result with R receivers and P ray paths is exported to CZML
- **THEN** the document SHALL contain `1 + R + P` packets (one document packet, R receiver points,
  P path polylines)

#### Scenario: Output is valid CZML JSON
- **WHEN** the CZML export is re-parsed
- **THEN** it SHALL be a JSON array whose first element is a document packet with an `id` of
  `document` and a CZML `version`

### Requirement: CZML georeferenced vs cartesian positions
CZML export SHALL encode positions as `cartographicDegrees` (longitude, latitude, height) using
the inverse of the scene georeference when the scene is georeferenced, and as `cartesian`
otherwise.

#### Scenario: Georeferenced scene emits cartographicDegrees
- **WHEN** a georeferenced scene's result is exported to CZML
- **THEN** receiver and path positions SHALL use `cartographicDegrees` derived from the local ENU
  coordinates via the inverse georeference

#### Scenario: Non-georeferenced scene emits cartesian
- **WHEN** a scene without a geographic origin is exported to CZML
- **THEN** positions SHALL use `cartesian` local coordinates
