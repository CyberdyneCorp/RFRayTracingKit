#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "rftrace/geometry.hpp"
#include "rftrace/math.hpp"

namespace rftrace {
class Scene;

namespace io {

/// True only in a GDAL-enabled build (RFTRACE_ENABLE_GDAL=ON). When false the
/// GeoTIFF DEM / heatmap entry points are still declared but throw a clear
/// "built without GDAL" runtime error.
bool gdalAvailable();

/// A digital elevation model imported from a single-band GeoTIFF: a regular grid
/// of absolute elevations (metres) plus the affine GDAL geotransform. Once a
/// projection origin has been recorded (`projected`), it also provides the local
/// East-North-Up positions of its posts and a triangulated terrain surface using
/// the same equirectangular projection as `Scene::geoProject` (D1).
///
/// The struct is plain data — it carries no GDAL types — so it can be stored in a
/// Scene and used for sampling in any build.
struct TerrainModel {
  int width = 0;   ///< number of columns (raster X, east)
  int height = 0;  ///< number of rows (raster Y, north)
  /// Row-major elevations, size width*height, absolute metres. No-data posts are
  /// stored as NaN.
  std::vector<double> elevations;
  /// GDAL affine geotransform mapping pixel (col,row) at the pixel corner to
  /// geographic coordinates: lon = gt[0]+col*gt[1]+row*gt[2],
  /// lat = gt[3]+col*gt[4]+row*gt[5].
  std::array<double, 6> geoTransform{0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  /// Raster no-data value, or NaN when the band declares none.
  double noData = std::numeric_limits<double>::quiet_NaN();
  /// Projection origin (degrees) used to place posts in local ENU metres.
  double originLat = 0.0;
  double originLon = 0.0;
  /// True once `originLat`/`originLon` have been set (mesh / ENU sampling valid).
  bool projected = false;

  bool empty() const { return width <= 0 || height <= 0; }

  /// Geographic coordinates (lon, lat) of the centre of post (col, row).
  void pixelCenterLonLat(int col, int row, double& lon, double& lat) const;

  /// Elevation stored at grid post (col, row) in absolute metres; NaN when the
  /// post is out of range or no-data.
  double postElevation(int col, int row) const;

  /// Bilinear elevation sample at geographic (lon, lat); NaN outside the raster.
  double elevationAtLonLat(double lon, double lat) const;

  /// Elevation sample at a local ENU position (x, y) in metres. Inverts the
  /// equirectangular projection about the recorded origin then samples the
  /// raster. Requires `projected`; NaN outside the raster.
  double elevationAt(double x, double y) const;

  /// Local ENU position (metres, Z=elevation) of post (col, row). Requires
  /// `projected`.
  Vec3 postLocal(int col, int row) const;

  /// Triangulated terrain surface in local ENU metres (two triangles per cell,
  /// upward-facing). Cells touching a no-data post are skipped. Requires
  /// `projected`.
  std::vector<Triangle> buildMesh() const;
};

/// Read a single-band GeoTIFF DEM (GDAL) into a `TerrainModel` (raster elevations
/// + geotransform + no-data). The result is not yet projected; call
/// `Scene::loadTerrain` (or set `originLat`/`originLon`/`projected`) to place it
/// in a local frame. Throws SceneError on I/O failure, and — in a build without
/// GDAL — a runtime error stating the library was built without GDAL.
TerrainModel loadGeoTiffDem(const std::string& path);

/// Options controlling `Scene::loadTerrain`.
struct TerrainImportOptions {
  /// Material assigned to the terrain surface (created if the scene lacks it).
  std::string terrainMaterial = "soil";
  /// When true, buildings added by later geospatial imports are lifted so their
  /// base sits on the terrain surface at the footprint centroid.
  bool offsetBuildingBases = false;
};

}  // namespace io
}  // namespace rftrace
