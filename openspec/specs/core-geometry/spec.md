# core-geometry Specification

## Purpose
TBD - created by archiving change phase1-cpu-prototype. Update Purpose after archive.
## Requirements
### Requirement: Vector and ray primitives
The library SHALL provide double-precision 3D vector, ray and triangle primitives built
on Eigen, usable by all backends and RF code without introducing a backend dependency.

#### Scenario: Vec3 is an Eigen-backed 3D vector
- **WHEN** application code constructs a `Vec3` from three doubles
- **THEN** the library SHALL expose element access, dot product, cross product, norm and
  normalization consistent with Eigen semantics

#### Scenario: Ray carries origin, direction and interval
- **WHEN** a `Ray` is created with an origin, a direction, and `tMin`/`tMax` bounds
- **THEN** the library SHALL normalize or accept the direction as documented and SHALL
  only report intersections with parametric distance `t` within `[tMin, tMax]`

### Requirement: Ray–triangle intersection
The library SHALL compute ray–triangle intersections using a Möller–Trumbore style test
and SHALL report barycentric coordinates and parametric distance for a hit.

#### Scenario: Ray intersects a triangle
- **WHEN** a ray passes through the interior of a triangle within its `t` interval
- **THEN** the library SHALL report a hit with distance `t`, barycentric `(u, v)`, and
  the index of the triangle

#### Scenario: Ray misses a triangle
- **WHEN** a ray does not cross the triangle plane inside the triangle within `[tMin, tMax]`
- **THEN** the library SHALL report no hit

#### Scenario: Degenerate and parallel cases are stable
- **WHEN** a ray is parallel to a triangle or the triangle is degenerate (zero area)
- **THEN** the library SHALL report no hit without dividing by zero or producing NaN

### Requirement: BVH construction
The library SHALL build a NanoRT-style bounding volume hierarchy over a triangle mesh so
that ray queries run in sublinear time relative to triangle count.

#### Scenario: BVH built over a triangle set
- **WHEN** a BVH is constructed from N triangles
- **THEN** the resulting hierarchy SHALL bound every triangle and SHALL be queryable for
  intersections

#### Scenario: Empty mesh
- **WHEN** a BVH is built over zero triangles
- **THEN** construction SHALL succeed and all subsequent ray queries SHALL report no hit

### Requirement: Closest-hit traversal
The library SHALL support closest-hit queries that return the nearest triangle
intersection along a ray.

#### Scenario: Nearest triangle is returned
- **WHEN** a ray crosses multiple triangles within its interval
- **THEN** the query SHALL return the intersection with the smallest parametric distance `t`

#### Scenario: Result matches brute-force reference
- **WHEN** the same ray is tested against the BVH and against a brute-force loop over all
  triangles
- **THEN** both SHALL agree on hit/miss and on the nearest triangle index within a
  documented floating-point tolerance

### Requirement: Occlusion (any-hit) traversal
The library SHALL support occlusion queries that report whether any triangle blocks a
segment between two points, used for line-of-sight visibility.

#### Scenario: Segment blocked by geometry
- **WHEN** an occlusion query is issued for a segment that a triangle crosses strictly
  between the endpoints
- **THEN** the query SHALL report the segment as blocked

#### Scenario: Clear segment
- **WHEN** no triangle crosses the segment within its interval
- **THEN** the query SHALL report the segment as visible (unoccluded)

#### Scenario: Endpoint self-intersection is avoided
- **WHEN** the segment endpoints lie on scene surfaces (e.g. a reflection point on a wall)
- **THEN** the query SHALL apply an epsilon offset so a surface does not spuriously occlude
  its own endpoint

