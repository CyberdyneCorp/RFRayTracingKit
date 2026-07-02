#include "rftrace/importers/geotiff_terrain.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "geo_import_util.hpp"
#include "rftrace/scene.hpp"

#if RFTRACE_HAVE_GDAL
#include <gdal_priv.h>
#endif

namespace rftrace::io {

namespace {
// Equirectangular ENU projection constants, matching Scene::geoProject (D1).
inline constexpr double kMetersPerDegLonEquator = 111320.0;
inline constexpr double kMetersPerDegLat = 110540.0;
inline constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
}  // namespace

// --- TerrainModel (pure C++, available in every build) ----------------------

void TerrainModel::pixelCenterLonLat(int col, int row, double& lon,
                                     double& lat) const {
  const double c = col + 0.5;
  const double r = row + 0.5;
  lon = geoTransform[0] + c * geoTransform[1] + r * geoTransform[2];
  lat = geoTransform[3] + c * geoTransform[4] + r * geoTransform[5];
}

double TerrainModel::postElevation(int col, int row) const {
  if (col < 0 || col >= width || row < 0 || row >= height) return kNaN;
  return elevations[static_cast<std::size_t>(row) * width + col];
}

double TerrainModel::elevationAtLonLat(double lon, double lat) const {
  if (empty()) return kNaN;
  // Invert the affine geotransform to fractional pixel-corner coordinates.
  const double det =
      geoTransform[1] * geoTransform[5] - geoTransform[2] * geoTransform[4];
  if (det == 0.0) return kNaN;
  const double dx = lon - geoTransform[0];
  const double dy = lat - geoTransform[3];
  const double px = (dx * geoTransform[5] - dy * geoTransform[2]) / det;
  const double py = (-dx * geoTransform[4] + dy * geoTransform[1]) / det;
  if (px < 0.0 || px > width || py < 0.0 || py > height) return kNaN;

  // Convert to post-centre coordinates and bilinearly interpolate.
  const double cf = std::clamp(px - 0.5, 0.0, width - 1.0);
  const double rf = std::clamp(py - 0.5, 0.0, height - 1.0);
  const int c0 = static_cast<int>(std::floor(cf));
  const int r0 = static_cast<int>(std::floor(rf));
  const int c1 = std::min(c0 + 1, width - 1);
  const int r1 = std::min(r0 + 1, height - 1);
  const double fc = cf - c0;
  const double fr = rf - r0;

  const double e00 = postElevation(c0, r0);
  const double e10 = postElevation(c1, r0);
  const double e01 = postElevation(c0, r1);
  const double e11 = postElevation(c1, r1);
  if (!std::isfinite(e00) || !std::isfinite(e10) || !std::isfinite(e01) ||
      !std::isfinite(e11))
    return kNaN;
  const double top = e00 * (1.0 - fc) + e10 * fc;
  const double bot = e01 * (1.0 - fc) + e11 * fc;
  return top * (1.0 - fr) + bot * fr;
}

double TerrainModel::elevationAt(double x, double y) const {
  const double lon =
      originLon + x / (kMetersPerDegLonEquator * std::cos(originLat * kDegToRad));
  const double lat = originLat + y / kMetersPerDegLat;
  return elevationAtLonLat(lon, lat);
}

Vec3 TerrainModel::postLocal(int col, int row) const {
  double lon = 0.0;
  double lat = 0.0;
  pixelCenterLonLat(col, row, lon, lat);
  const double x = (lon - originLon) * kMetersPerDegLonEquator *
                   std::cos(originLat * kDegToRad);
  const double y = (lat - originLat) * kMetersPerDegLat;
  return Vec3(x, y, postElevation(col, row));
}

std::vector<Triangle> TerrainModel::buildMesh() const {
  std::vector<Triangle> tris;
  if (width < 2 || height < 2) return tris;
  tris.reserve(static_cast<std::size_t>(width - 1) * (height - 1) * 2);
  for (int r = 0; r + 1 < height; ++r) {
    for (int c = 0; c + 1 < width; ++c) {
      if (!std::isfinite(postElevation(c, r)) ||
          !std::isfinite(postElevation(c + 1, r)) ||
          !std::isfinite(postElevation(c + 1, r + 1)) ||
          !std::isfinite(postElevation(c, r + 1)))
        continue;
      const Vec3 p00 = postLocal(c, r);
      const Vec3 p10 = postLocal(c + 1, r);
      const Vec3 p11 = postLocal(c + 1, r + 1);
      const Vec3 p01 = postLocal(c, r + 1);
      // Wound counter-clockwise viewed from above so normals face +Z.
      tris.push_back(Triangle{p00, p10, p11});
      tris.push_back(Triangle{p00, p11, p01});
    }
  }
  return tris;
}

// --- GDAL-gated entry points ------------------------------------------------

bool gdalAvailable() {
#if RFTRACE_HAVE_GDAL
  return true;
#else
  return false;
#endif
}

#if RFTRACE_HAVE_GDAL

TerrainModel loadGeoTiffDem(const std::string& path) {
  GDALAllRegister();
  auto* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
  if (!ds) throw SceneError("cannot open GeoTIFF DEM '" + path + "'");

  TerrainModel tm;
  double gt[6];
  if (ds->GetGeoTransform(gt) == CE_None)
    std::copy(gt, gt + 6, tm.geoTransform.begin());

  GDALRasterBand* band = ds->GetRasterBand(1);
  if (!band) {
    GDALClose(ds);
    throw SceneError("GeoTIFF DEM '" + path + "' has no raster band");
  }
  tm.width = band->GetXSize();
  tm.height = band->GetYSize();
  tm.elevations.assign(static_cast<std::size_t>(tm.width) * tm.height, 0.0);

  const CPLErr err =
      band->RasterIO(GF_Read, 0, 0, tm.width, tm.height, tm.elevations.data(),
                     tm.width, tm.height, GDT_Float64, 0, 0);
  if (err != CE_None) {
    GDALClose(ds);
    throw SceneError("failed to read GeoTIFF DEM band '" + path + "'");
  }

  int hasNoData = 0;
  const double nd = band->GetNoDataValue(&hasNoData);
  if (hasNoData) {
    tm.noData = nd;
    for (double& v : tm.elevations)
      if (v == nd) v = kNaN;
  }
  GDALClose(ds);
  return tm;
}

#else  // !RFTRACE_HAVE_GDAL

TerrainModel loadGeoTiffDem(const std::string& /*path*/) {
  throw std::runtime_error(
      "GeoTIFF DEM import requires GDAL, but the library was built without GDAL "
      "(configure with -DRFTRACE_ENABLE_GDAL=ON)");
}

#endif  // RFTRACE_HAVE_GDAL

}  // namespace rftrace::io

// --- Scene::loadTerrain (always compiled; the GDAL dependency lives entirely
//     in io::loadGeoTiffDem, which throws when built without GDAL) ------------
namespace rftrace {

std::size_t Scene::loadTerrain(const std::string& path,
                               const io::TerrainImportOptions& opts) {
  io::TerrainModel tm = io::loadGeoTiffDem(path);
  if (tm.empty())
    throw SceneError("GeoTIFF DEM '" + path + "' contains no raster data");

  // Default the georeference to the DEM's geographic centre when unset.
  if (!hasGeoOrigin()) {
    const double cLon = tm.geoTransform[0] +
                        (tm.width * 0.5) * tm.geoTransform[1] +
                        (tm.height * 0.5) * tm.geoTransform[2];
    const double cLat = tm.geoTransform[3] +
                        (tm.width * 0.5) * tm.geoTransform[4] +
                        (tm.height * 0.5) * tm.geoTransform[5];
    setGeoOrigin(cLat, cLon);
  }
  tm.originLat = coordinateSystem_.originLat;
  tm.originLon = coordinateSystem_.originLon;
  tm.projected = true;

  io::detail::ensureMaterial(*this, opts.terrainMaterial);
  std::vector<Triangle> tris = tm.buildMesh();
  addMesh(tris, opts.terrainMaterial);

  terrain_ = std::move(tm);
  offsetBuildingBases_ = opts.offsetBuildingBases;
  return tris.size();
}

}  // namespace rftrace
