#include "rftrace/importers/osm_pbf_importer.hpp"

#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "osm_extract.hpp"
#include "rftrace/scene.hpp"

#if RFTRACE_HAVE_OSMIUM
#include <osmium/handler.hpp>
#include <osmium/io/pbf_input.hpp>
#include <osmium/io/reader.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/types.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/visitor.hpp>
#endif

namespace rftrace::io {

bool osmiumAvailable() {
#if RFTRACE_HAVE_OSMIUM
  return true;
#else
  return false;
#endif
}

#if RFTRACE_HAVE_OSMIUM

namespace {
/// Collects nodes (id -> lat/lon) and ways (refs + tags) from a PBF stream into
/// the format-agnostic representation shared with the XML/Overpass readers, so
/// building/vegetation extraction happens once in detail::buildOsmScene.
struct OsmCollector : public osmium::handler::Handler {
  std::unordered_map<std::int64_t, detail::LatLon> nodes;
  std::vector<detail::RawWay> ways;

  void node(const osmium::Node& n) {
    const osmium::Location loc = n.location();
    if (!loc.valid()) return;
    nodes[static_cast<std::int64_t>(n.id())] = {loc.lat(), loc.lon()};
  }

  void way(const osmium::Way& w) {
    detail::RawWay rw;
    rw.refs.reserve(w.nodes().size());
    for (const auto& nr : w.nodes())
      rw.refs.push_back(static_cast<std::int64_t>(nr.ref()));
    for (const auto& t : w.tags()) rw.tags[t.key()] = t.value();
    ways.push_back(std::move(rw));
  }
};
}  // namespace

std::size_t importOSMPbf(Scene& scene, const std::string& path,
                         const OsmImportOptions& opts) {
  OsmCollector collector;
  try {
    osmium::io::Reader reader{
        path, osmium::osm_entity_bits::node | osmium::osm_entity_bits::way};
    osmium::apply(reader, collector);
    reader.close();
  } catch (const std::exception& e) {
    throw SceneError(std::string("cannot read OSM PBF '") + path +
                     "': " + e.what());
  }

  return detail::buildOsmScene(
      scene, collector.nodes, collector.ways, opts,
      "OSM PBF contains no building or vegetation ways");
}

#else  // !RFTRACE_HAVE_OSMIUM

std::size_t importOSMPbf(Scene& /*scene*/, const std::string& /*path*/,
                         const OsmImportOptions& /*opts*/) {
  throw std::runtime_error(
      "OSM PBF import requires libosmium, but the library was built without OSM "
      "PBF (osmium) (configure with -DRFTRACE_ENABLE_OSMIUM=ON)");
}

#endif  // RFTRACE_HAVE_OSMIUM

}  // namespace rftrace::io

namespace rftrace {

std::size_t Scene::loadOSMPbf(const std::string& path,
                              const std::string& buildingMaterial,
                              const std::string& vegetationMaterial) {
  io::OsmImportOptions opts;
  opts.buildingMaterial = buildingMaterial;
  opts.vegetationMaterial = vegetationMaterial;
  return io::importOSMPbf(*this, path, opts);
}

}  // namespace rftrace
