#pragma once

#include <string>
#include <utility>
#include <vector>

#include "rftrace/scene.hpp"

// Internal helpers shared by the always-on geospatial importers (GeoJSON,
// CityJSON, OSM). Not installed as a public header.
namespace rftrace::io::detail {

/// Set the scene's geographic origin to the centroid of the supplied
/// (lat, lon) samples when the scene has no origin yet (task 1.3). No-op if the
/// scene is already georeferenced or `latLon` is empty.
void ensureGeoOrigin(Scene& scene,
                     const std::vector<std::pair<double, double>>& latLon);

/// Ensure a material named `name` exists in the scene, adding a built-in preset
/// (or a neutral default carrying the name) when it is absent. A blank name is
/// left alone (the caller then uses the scene default material).
void ensureMaterial(Scene& scene, const std::string& name);

/// Base elevation (metres, local Z) for an extruded footprint. Returns the
/// terrain elevation at the footprint centroid when the scene has terrain and
/// building-base offsetting was requested (`Scene::offsetBuildingBases`);
/// otherwise 0. `ringLocal` is the footprint ring already projected to local
/// ENU metres.
double footprintBaseZ(const Scene& scene, const std::vector<Vec3>& ringLocal);

}  // namespace rftrace::io::detail
