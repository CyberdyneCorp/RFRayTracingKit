#pragma once

#include <string>

namespace rftrace {
struct CoverageResult;
class Scene;

namespace io {

/// Which coverage metric a GeoTIFF heatmap pixel carries.
enum class CoverageMetric { PowerDbm, PathLossDb, SinrDb };

/// Options for `exportCoverageGeoTiff`.
struct GeoTiffHeatmapOptions {
  CoverageMetric metric = CoverageMetric::PowerDbm;
  /// Value written for uncovered / non-finite cells (also set as the band's
  /// no-data value).
  double noData = -9999.0;
  /// Geographic origin (degrees) of the local ENU frame. When `georeferenced`
  /// is true the output carries a WGS84 lon/lat geotransform; otherwise it
  /// carries a plain ENU-metre geotransform with no CRS.
  double originLat = 0.0;
  double originLon = 0.0;
  bool georeferenced = false;
};

/// Export a `CoverageResult` to a georeferenced single-band Float32 GeoTIFF
/// (GDAL). The geotransform is derived from the grid origin/cell size plus the
/// georeference in `opts`; uncovered cells are written as `opts.noData`.
/// Throws SceneError on write failure, and — in a build without GDAL — a runtime
/// error stating the library was built without GDAL.
void exportCoverageGeoTiff(const CoverageResult& result, const std::string& path,
                           const GeoTiffHeatmapOptions& opts = {});

/// Convenience overload that fills the georeference in `opts` from `scene`
/// (WGS84 lon/lat when the scene is georeferenced) before writing.
void exportCoverageGeoTiff(const Scene& scene, const CoverageResult& result,
                           const std::string& path,
                           GeoTiffHeatmapOptions opts = {});

}  // namespace io
}  // namespace rftrace
