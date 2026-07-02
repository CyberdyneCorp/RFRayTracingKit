#pragma once

#include <cstddef>
#include <string>

#include "rftrace/importers/osm_importer.hpp"

namespace rftrace::io {

/// True only in an osmium-enabled build (RFTRACE_ENABLE_OSMIUM=ON). When false
/// the OSM PBF entry points are still declared but throw a clear "built without
/// OSM PBF (osmium)" runtime error.
bool osmiumAvailable();

/// Import an OpenStreetMap `.osm.pbf` binary document into `scene` using
/// libosmium (header-only) + protozero. Nodes and ways are collected via an
/// `osmium::io::Reader` and handler, then run through the SAME
/// building/vegetation extraction as the Overpass-JSON and `.osm` XML readers:
/// `building` ways become extruded buildings (height from `height`, else
/// `building:levels` * 3 m, else `opts.defaultBuildingHeight`), and
/// `natural=wood`/`landuse=forest`/`leisure=park` areas become vegetation
/// extruded to `opts.vegetationHeight`. Coordinates are projected through the
/// scene georeference (defaulted to the dataset centroid when unset). Returns
/// the number of triangles added.
///
/// Throws SceneError when the file cannot be read or yields no geometry, and —
/// in a build without osmium — a runtime error stating the library was built
/// without OSM PBF support.
std::size_t importOSMPbf(Scene& scene, const std::string& path,
                         const OsmImportOptions& opts = {});

}  // namespace rftrace::io
