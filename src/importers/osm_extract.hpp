#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rftrace/importers/osm_importer.hpp"

// Internal helpers shared by every OpenStreetMap reader (Overpass JSON, `.osm`
// XML, and `.osm.pbf`). Not installed as a public header. The three readers each
// produce a node table and a list of ways in this common representation, then
// call `buildOsmScene`, so building/vegetation extraction, extrusion, and
// georeferencing are defined exactly once.
namespace rftrace::io::detail {

/// (latitude, longitude) in degrees.
using LatLon = std::pair<double, double>;

/// Flat OSM tag map (string key -> string value).
using Tags = std::map<std::string, std::string>;

/// A parsed way: ordered node references plus its tags. Format-agnostic so all
/// readers feed the same extraction.
struct RawWay {
  std::vector<std::int64_t> refs;
  Tags tags;
};

/// Shared extraction: resolve way rings against the node table, filter to
/// building/vegetation ways, extrude, and mutate the scene. Buildings take their
/// height from `height`, else `building:levels` times a 3 m storey height, else
/// `opts.defaultBuildingHeight`; `natural=wood`/`landuse=forest`/`leisure=park`
/// areas become vegetation extruded to `opts.vegetationHeight`. Coordinates are
/// projected through the scene georeference (defaulted to the dataset centroid
/// when unset). `emptyMsg` is thrown as a SceneError when no geometry results.
std::size_t buildOsmScene(Scene& scene,
                          const std::unordered_map<std::int64_t, LatLon>& nodes,
                          const std::vector<RawWay>& ways,
                          const OsmImportOptions& opts, const char* emptyMsg);

}  // namespace rftrace::io::detail
