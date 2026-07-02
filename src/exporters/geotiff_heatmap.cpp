#include "rftrace/exporters/geotiff_heatmap.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "rftrace/coverage.hpp"
#include "rftrace/scene.hpp"

#if RFTRACE_HAVE_GDAL
#include <cpl_conv.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#endif

namespace rftrace::io {

namespace {
inline constexpr double kMetersPerDegLonEquator = 111320.0;
inline constexpr double kMetersPerDegLat = 110540.0;
inline constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

/// Select the coverage metric for a cell, or NaN when unavailable/uncovered.
double metricValue(const CoverageResult& r, CoverageMetric metric, int idx) {
  const std::vector<double>* src = nullptr;
  switch (metric) {
    case CoverageMetric::PowerDbm:
      src = &r.powerDbm;
      break;
    case CoverageMetric::PathLossDb:
      src = &r.pathLossDb;
      break;
    case CoverageMetric::SinrDb:
      src = &r.sinrDb;
      break;
  }
  if (!src || idx < 0 || idx >= static_cast<int>(src->size()))
    return std::numeric_limits<double>::quiet_NaN();
  return (*src)[idx];
}
}  // namespace

#if RFTRACE_HAVE_GDAL

void exportCoverageGeoTiff(const CoverageResult& result, const std::string& path,
                           const GeoTiffHeatmapOptions& opts) {
  const CoverageGrid& grid = result.grid;
  const int cols = grid.cols;
  const int rows = grid.rows;
  if (cols <= 0 || rows <= 0)
    throw SceneError("cannot export an empty coverage grid to GeoTIFF");

  // Geotransform: top-left corner + pixel size. GeoTIFF rows run north->south,
  // so raster row 0 is the grid's northern edge.
  double gt[6];
  if (opts.georeferenced) {
    const double mPerDegLon =
        kMetersPerDegLonEquator * std::cos(opts.originLat * kDegToRad);
    const double west = opts.originLon + grid.origin.x() / mPerDegLon;
    const double north =
        opts.originLat + (grid.origin.y() + rows * grid.cellSize) /
                             kMetersPerDegLat;
    gt[0] = west;
    gt[1] = grid.cellSize / mPerDegLon;
    gt[2] = 0.0;
    gt[3] = north;
    gt[4] = 0.0;
    gt[5] = -grid.cellSize / kMetersPerDegLat;
  } else {
    gt[0] = grid.origin.x();
    gt[1] = grid.cellSize;
    gt[2] = 0.0;
    gt[3] = grid.origin.y() + rows * grid.cellSize;
    gt[4] = 0.0;
    gt[5] = -grid.cellSize;
  }

  GDALAllRegister();
  GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
  if (!drv) throw SceneError("GDAL GTiff driver is unavailable");
  GDALDataset* ds = drv->Create(path.c_str(), cols, rows, 1, GDT_Float32, nullptr);
  if (!ds) throw SceneError("cannot create GeoTIFF '" + path + "'");

  ds->SetGeoTransform(gt);
  if (opts.georeferenced) {
    OGRSpatialReference srs;
    srs.SetWellKnownGeogCS("WGS84");
    char* wkt = nullptr;
    if (srs.exportToWkt(&wkt) == OGRERR_NONE && wkt) {
      ds->SetProjection(wkt);
      CPLFree(wkt);
    }
  }

  GDALRasterBand* band = ds->GetRasterBand(1);
  band->SetNoDataValue(opts.noData);

  // Fill the raster, flipping vertically so raster row 0 is the northern edge.
  std::vector<float> buf(static_cast<std::size_t>(cols) * rows);
  for (int rr = 0; rr < rows; ++rr) {
    const int gridRow = rows - 1 - rr;
    for (int c = 0; c < cols; ++c) {
      const double v = metricValue(result, opts.metric, gridRow * cols + c);
      buf[static_cast<std::size_t>(rr) * cols + c] =
          std::isfinite(v) ? static_cast<float>(v)
                           : static_cast<float>(opts.noData);
    }
  }

  const CPLErr err = band->RasterIO(GF_Write, 0, 0, cols, rows, buf.data(), cols,
                                    rows, GDT_Float32, 0, 0);
  GDALClose(ds);
  if (err != CE_None)
    throw SceneError("failed to write GeoTIFF band '" + path + "'");
}

#else  // !RFTRACE_HAVE_GDAL

void exportCoverageGeoTiff(const CoverageResult& /*result*/,
                           const std::string& /*path*/,
                           const GeoTiffHeatmapOptions& /*opts*/) {
  throw std::runtime_error(
      "GeoTIFF heatmap export requires GDAL, but the library was built without "
      "GDAL (configure with -DRFTRACE_ENABLE_GDAL=ON)");
}

#endif  // RFTRACE_HAVE_GDAL

void exportCoverageGeoTiff(const Scene& scene, const CoverageResult& result,
                           const std::string& path, GeoTiffHeatmapOptions opts) {
  const CoordinateSystem& cs = scene.coordinateSystem();
  opts.georeferenced = cs.georeferenced;
  opts.originLat = cs.originLat;
  opts.originLon = cs.originLon;
  exportCoverageGeoTiff(result, path, opts);
}

}  // namespace rftrace::io
