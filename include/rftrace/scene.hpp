#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "rftrace/antenna.hpp"
#include "rftrace/geometry.hpp"
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
/// frame in metres with Z as height.
struct CoordinateSystem {
  UpAxis up = UpAxis::Z;
  std::string units = "meters";
  bool georeferenced = false;
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

  // --- Import helpers (implemented in the importers) ------------------------
  /// Load a triangle mesh (glTF/OBJ) via Assimp, normalizing to Z-up. When
  /// `material` is non-empty it must name an existing material.
  std::size_t loadMesh(const std::string& path, const std::string& material = "");
  /// Load material definitions from a JSON file into the material table.
  std::size_t loadMaterials(const std::string& path);

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
};

}  // namespace rftrace
