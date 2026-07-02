#pragma once

#include <cstddef>
#include <string>

#include "rftrace/scene.hpp"

namespace rftrace::io {

/// Options controlling OpenStreetMap (Overpass JSON) import.
struct OsmImportOptions {
  /// Material for extruded building meshes (created if the scene lacks it).
  std::string buildingMaterial = "concrete";
  /// Material for vegetation meshes (created if the scene lacks it).
  std::string vegetationMaterial = "vegetation";
  /// Fallback building height (metres) when neither `height` nor
  /// `building:levels` is tagged.
  double defaultBuildingHeight = 6.0;
  /// Extrusion height (metres) for vegetation areas.
  double vegetationHeight = 8.0;
};

/// Import an OpenStreetMap document into `scene`, autodetecting the Overpass
/// JSON and `.osm` XML formats by content (a leading `<` selects the XML reader,
/// otherwise the Overpass-JSON reader). Node lat/lon are resolved for each way;
/// ways carrying a `building` tag are extruded into buildings (height from
/// `height`, else `building:levels` times a 3 m storey height, else
/// `opts.defaultBuildingHeight`), and areas tagged `natural=wood`,
/// `landuse=forest`, or `leisure=park` become vegetation extruded to
/// `opts.vegetationHeight`.
///
/// Coordinates are projected through the scene georeference; when the scene has
/// no origin it is set to the dataset centroid first. Returns the number of
/// triangles added. Throws SceneError when the file is missing, is not valid
/// OSM, or yields no geometry (the scene is left unchanged).
std::size_t importOSM(Scene& scene, const std::string& path,
                      const OsmImportOptions& opts = {});

/// Import an OpenStreetMap `.osm` XML document into `scene` using the in-tree
/// XML reader (no external XML dependency). Parses `<node id lat lon>`,
/// `<way>` with its `<nd ref/>` node references and `<tag k v/>` tags, then
/// applies the SAME building/vegetation extraction as `importOSM`. Throws
/// SceneError when the file is missing, is malformed XML, or yields no geometry.
std::size_t importOSMXml(Scene& scene, const std::string& path,
                         const OsmImportOptions& opts = {});

}  // namespace rftrace::io
