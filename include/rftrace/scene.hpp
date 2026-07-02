#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "rftrace/antenna.hpp"
#include "rftrace/geometry.hpp"
#include "rftrace/importers/geotiff_terrain.hpp"
#include "rftrace/material.hpp"
#include "rftrace/rf/array.hpp"

namespace rftrace {

/// Thrown for invalid scene operations (duplicate ids, unknown materials, ...).
struct SceneError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

/// Up-axis convention of the scene. The core is right-handed Z-up.
enum class UpAxis { Z };

/// Records how scene coordinates are interpreted. Phase 1 is a local Cartesian
/// frame in metres with Z as height. When `georeferenced` is true the frame is a
/// local East-North-Up tangent plane anchored at (`originLat`, `originLon`); see
/// Scene::setGeoOrigin / Scene::geoProject.
struct CoordinateSystem {
  UpAxis up = UpAxis::Z;
  std::string units = "meters";
  bool georeferenced = false;
  double originLat = 0.0;  ///< Geographic origin latitude in degrees (WGS84).
  double originLon = 0.0;  ///< Geographic origin longitude in degrees (WGS84).
};

struct Transmitter {
  std::string id;
  Vec3 position{0, 0, 0};
  double frequencyHz = 3.5e9;
  double powerDbm = 43.0;
  AntennaPattern antenna = AntennaPattern::Omnidirectional();
  Polarization polarization = Polarization::Vertical;
  /// Optional antenna array. When set, its steered gain replaces the single
  /// `antenna` gain in the link budget. `beamSteering` is the main-beam
  /// direction; a zero vector steers toward each path's departure direction.
  std::optional<rf::AntennaArray> array;
  Vec3 beamSteering{0, 0, 0};
};

struct Receiver {
  std::string id;
  Vec3 position{0, 0, 0};
  AntennaPattern antenna = AntennaPattern::Omnidirectional();
  Polarization polarization = Polarization::Vertical;
  std::optional<rf::AntennaArray> array;
  Vec3 beamSteering{0, 0, 0};
};

/// The single backend-agnostic input to a simulation. Geometry is stored as a
/// flat triangle soup with a parallel per-triangle material index so a ray hit
/// (triangle index) resolves directly to a material.
class Scene {
 public:
  Scene();

  // --- Materials ------------------------------------------------------------
  /// Add (or replace by name) a material. Returns its index.
  int addMaterial(const Material& material);
  /// Index of a material by name, or nullopt if absent.
  std::optional<int> materialIndex(const std::string& name) const;
  const Material& material(int index) const { return materials_.at(index); }
  const std::vector<Material>& materials() const { return materials_; }

  // --- Geometry -------------------------------------------------------------
  /// Append triangles assigned to a named material. The material must exist.
  void addMesh(const std::vector<Triangle>& triangles,
               const std::string& materialName);
  /// Append triangles assigned to a material index (-1 => default material).
  void addMesh(const std::vector<Triangle>& triangles, int materialIndex);

  const std::vector<Triangle>& triangles() const { return triangles_; }
  int triangleMaterialIndex(int triangle) const {
    return triangleMaterial_.at(triangle);
  }
  const Material& materialForTriangle(int triangle) const {
    return materials_.at(triangleMaterial_.at(triangle));
  }

  // --- Transmitters / receivers --------------------------------------------
  void addTransmitter(const Transmitter& tx);
  void addReceiver(const Receiver& rx);
  const std::vector<Transmitter>& transmitters() const { return transmitters_; }
  const std::vector<Receiver>& receivers() const { return receivers_; }

  // --- Coordinate system ----------------------------------------------------
  CoordinateSystem& coordinateSystem() { return coordinateSystem_; }
  const CoordinateSystem& coordinateSystem() const { return coordinateSystem_; }

  // --- Georeferencing -------------------------------------------------------
  /// Anchor the scene's local ENU frame at a geographic origin (degrees, WGS84).
  /// Marks the coordinate system as georeferenced. All geospatial importers
  /// project onto this frame; if none is set when one runs, it defaults to the
  /// dataset centroid.
  void setGeoOrigin(double latDeg, double lonDeg);
  /// True once a geographic origin has been set.
  bool hasGeoOrigin() const { return coordinateSystem_.georeferenced; }
  /// Project a geographic point (degrees, altitude in metres) into local ENU
  /// metres using an equirectangular approximation about the origin:
  ///   x = (lon - lon0) * 111320 * cos(lat0),  y = (lat - lat0) * 110540,  z = alt.
  /// Right-handed, Z-up. Throws SceneError if no origin has been set.
  Vec3 geoProject(double latDeg, double lonDeg, double altMeters = 0.0) const;

  // --- Import helpers (implemented in the importers) ------------------------
  /// Load a triangle mesh (glTF/OBJ) via Assimp, normalizing to Z-up. When
  /// `material` is non-empty it must name an existing material.
  std::size_t loadMesh(const std::string& path, const std::string& material = "");
  /// Load material definitions from a JSON file into the material table.
  std::size_t loadMaterials(const std::string& path);
  /// Import a GeoJSON FeatureCollection: Polygon/MultiPolygon features become
  /// extruded buildings assigned `buildingMaterial`, Point features become
  /// receivers or transmitters per `pointType` ("receiver"/"transmitter"/"").
  /// Coordinates are projected through the georeference (centroid default when
  /// unset). Returns the number of building triangles added.
  std::size_t loadGeoJSON(const std::string& path,
                          const std::string& buildingMaterial = "concrete",
                          const std::string& pointType = "receiver");
  /// Import a CityJSON document: Building/BuildingPart objects become meshes
  /// (triangulated boundaries, or footprint+height extrusion). Vertices are
  /// dequantized via the document transform then projected through the
  /// georeference. Returns the number of triangles added.
  std::size_t loadCityJSON(const std::string& path,
                           const std::string& buildingMaterial = "concrete");
  /// Import an Overpass (OSM) JSON document: `building` ways become extruded
  /// buildings and `natural=wood`/`landuse=forest`/`leisure=park` areas become
  /// vegetation. Node coordinates are resolved and projected through the
  /// georeference. Returns the number of triangles added.
  std::size_t loadOSM(const std::string& path,
                      const std::string& buildingMaterial = "concrete",
                      const std::string& vegetationMaterial = "vegetation");
  /// Load a single-band GeoTIFF DEM (GDAL) as a triangulated terrain surface
  /// assigned `opts.terrainMaterial` ("soil"). Sets the scene georeference to the
  /// DEM centroid when unset, stores the imported terrain (see `terrain()`), and
  /// — when `opts.offsetBuildingBases` is set — lifts subsequently-imported
  /// buildings onto the terrain. Returns the number of terrain triangles added.
  /// Throws SceneError on failure, or a runtime error when built without GDAL.
  std::size_t loadTerrain(const std::string& path,
                          const io::TerrainImportOptions& opts = {});

  // --- Terrain --------------------------------------------------------------
  /// True once a terrain DEM has been loaded via `loadTerrain`.
  bool hasTerrain() const { return terrain_.has_value(); }
  /// The imported terrain model, or nullptr when none has been loaded.
  const io::TerrainModel* terrain() const {
    return terrain_ ? &*terrain_ : nullptr;
  }
  /// Terrain elevation (absolute metres) at local ENU (x, y); 0 when no terrain
  /// is loaded or the point falls outside the DEM.
  double groundElevationAt(double x, double y) const;
  /// Whether imported buildings should be lifted onto the terrain (set by
  /// `loadTerrain` via `TerrainImportOptions::offsetBuildingBases`).
  bool offsetBuildingBases() const { return offsetBuildingBases_; }

 private:
  std::vector<Material> materials_;
  std::unordered_map<std::string, int> materialByName_;
  std::vector<Triangle> triangles_;
  std::vector<int> triangleMaterial_;
  std::vector<Transmitter> transmitters_;
  std::vector<Receiver> receivers_;
  std::unordered_map<std::string, std::size_t> txById_;
  std::unordered_map<std::string, std::size_t> rxById_;
  CoordinateSystem coordinateSystem_;
  int defaultMaterialIndex_ = 0;
  std::optional<io::TerrainModel> terrain_;
  bool offsetBuildingBases_ = false;
};

}  // namespace rftrace
