// GeoTIFF DEM import + coverage heatmap export tests.
//
// The GDAL-backed assertions are compiled only when RFTRACE_HAVE_GDAL is defined
// (RFTRACE_ENABLE_GDAL=ON). In the default build the file still contributes a
// test that verifies the graceful-degradation contract: gdalAvailable() is false
// and the entry points throw.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "rftrace/coverage.hpp"
#include "rftrace/exporters/geotiff_heatmap.hpp"
#include "rftrace/importers/geotiff_terrain.hpp"
#include "rftrace/scene.hpp"

using namespace rftrace;

#if RFTRACE_HAVE_GDAL

#include <cmath>
#include <vector>

#include <cpl_conv.h>
#include <gdal_priv.h>

namespace {

// Synthetic north-up DEM: 4 columns x 3 rows, 0.001-degree posts anchored at the
// top-left corner (lon 10.0, lat 50.0). Elevation of post (col,row) is a known
// affine function so exact-post samples are trivially verifiable.
constexpr int kW = 4;
constexpr int kH = 3;
constexpr double kLon0 = 10.0;
constexpr double kLat0 = 50.0;
constexpr double kPix = 0.001;

double postElev(int col, int row) { return 100.0 + row * 10.0 + col; }

std::string writeSyntheticDem() {
  const std::string path = ::testing::TempDir() + "/rftrace_dem.tif";
  GDALAllRegister();
  GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
  GDALDataset* ds = drv->Create(path.c_str(), kW, kH, 1, GDT_Float64, nullptr);
  double gt[6] = {kLon0, kPix, 0.0, kLat0, 0.0, -kPix};
  ds->SetGeoTransform(gt);
  std::vector<double> data(static_cast<std::size_t>(kW) * kH);
  for (int r = 0; r < kH; ++r)
    for (int c = 0; c < kW; ++c) data[r * kW + c] = postElev(c, r);
  ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, kW, kH, data.data(), kW, kH,
                                 GDT_Float64, 0, 0);
  GDALClose(ds);
  return path;
}

}  // namespace

TEST(GeoTiff, Availability) { EXPECT_TRUE(io::gdalAvailable()); }

TEST(GeoTiff, LoadDemReadsRasterAndGeotransform) {
  const std::string path = writeSyntheticDem();
  io::TerrainModel tm = io::loadGeoTiffDem(path);

  EXPECT_EQ(tm.width, kW);
  EXPECT_EQ(tm.height, kH);
  EXPECT_NEAR(tm.geoTransform[0], kLon0, 1e-9);
  EXPECT_NEAR(tm.geoTransform[1], kPix, 1e-9);
  EXPECT_NEAR(tm.geoTransform[3], kLat0, 1e-9);
  EXPECT_NEAR(tm.geoTransform[5], -kPix, 1e-9);
  for (int r = 0; r < kH; ++r)
    for (int c = 0; c < kW; ++c)
      EXPECT_NEAR(tm.postElevation(c, r), postElev(c, r), 1e-9);
}

TEST(GeoTiff, ElevationSamplerMatchesPostsInLonLat) {
  io::TerrainModel tm = io::loadGeoTiffDem(writeSyntheticDem());
  for (int r = 0; r < kH; ++r)
    for (int c = 0; c < kW; ++c) {
      double lon = 0.0;
      double lat = 0.0;
      tm.pixelCenterLonLat(c, r, lon, lat);
      EXPECT_NEAR(tm.elevationAtLonLat(lon, lat), postElev(c, r), 1e-6);
    }
  // A point clearly outside the raster extent yields NaN.
  EXPECT_TRUE(std::isnan(tm.elevationAtLonLat(kLon0 - 1.0, kLat0)));
}

TEST(GeoTiff, LoadTerrainBuildsMeshAndSetsCentroidOrigin) {
  Scene scene;
  const std::size_t tris = scene.loadTerrain(writeSyntheticDem());

  // (kW-1) * (kH-1) cells, two triangles each.
  EXPECT_EQ(tris, static_cast<std::size_t>((kW - 1) * (kH - 1) * 2));
  EXPECT_TRUE(scene.hasGeoOrigin());
  ASSERT_NE(scene.terrain(), nullptr);

  // Origin defaulted to the DEM's geographic centre.
  EXPECT_NEAR(scene.coordinateSystem().originLon, kLon0 + (kW * 0.5) * kPix,
              1e-9);
  EXPECT_NEAR(scene.coordinateSystem().originLat, kLat0 - (kH * 0.5) * kPix,
              1e-9);

  // The ENU elevation sampler reproduces a raster post at its local coordinates.
  const io::TerrainModel& tm = *scene.terrain();
  const Vec3 local = tm.postLocal(1, 1);
  EXPECT_NEAR(scene.groundElevationAt(local.x(), local.y()), postElev(1, 1),
              1e-4);
}

TEST(GeoTiff, ExportCoverageRoundTrips) {
  Scene scene;
  scene.setGeoOrigin(kLat0, kLon0);

  CoverageResult cr;
  cr.grid.origin = Vec3(0.0, 0.0, 0.0);
  cr.grid.cellSize = 100.0;
  cr.grid.cols = 2;
  cr.grid.rows = 2;
  cr.powerDbm = {-50.0, -60.0, -70.0, CoverageResult::NoSignal};

  const std::string path = ::testing::TempDir() + "/rftrace_cov.tif";
  io::exportCoverageGeoTiff(scene, cr, path);

  GDALAllRegister();
  auto* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
  ASSERT_NE(ds, nullptr);
  EXPECT_EQ(ds->GetRasterCount(), 1);
  EXPECT_EQ(ds->GetRasterXSize(), 2);
  EXPECT_EQ(ds->GetRasterYSize(), 2);

  double gt[6];
  ASSERT_EQ(ds->GetGeoTransform(gt), CE_None);
  const double mPerDegLon = 111320.0 * std::cos(kLat0 * M_PI / 180.0);
  EXPECT_NEAR(gt[0], kLon0, 1e-9);
  EXPECT_NEAR(gt[1], 100.0 / mPerDegLon, 1e-12);
  EXPECT_NEAR(gt[3], kLat0 + (2 * 100.0) / 110540.0, 1e-9);
  EXPECT_NEAR(gt[5], -100.0 / 110540.0, 1e-12);

  GDALRasterBand* band = ds->GetRasterBand(1);
  int hasNoData = 0;
  const double nd = band->GetNoDataValue(&hasNoData);
  EXPECT_TRUE(hasNoData);
  EXPECT_NEAR(nd, -9999.0, 1e-6);

  float buf[4];
  ASSERT_EQ(band->RasterIO(GF_Read, 0, 0, 2, 2, buf, 2, 2, GDT_Float32, 0, 0),
            CE_None);
  // Raster row 0 is the northern edge = grid row 1 (rows-1). Column order east.
  EXPECT_NEAR(buf[0], -70.0f, 1e-4);   // grid (row1,col0)
  EXPECT_NEAR(buf[1], -9999.0f, 1e-3);  // grid (row1,col1) = NoSignal -> nodata
  EXPECT_NEAR(buf[2], -50.0f, 1e-4);   // grid (row0,col0)
  EXPECT_NEAR(buf[3], -60.0f, 1e-4);   // grid (row0,col1)
  GDALClose(ds);
}

#else  // !RFTRACE_HAVE_GDAL

TEST(GeoTiff, GracefulWithoutGdal) {
  EXPECT_FALSE(io::gdalAvailable());

  Scene scene;
  EXPECT_THROW(scene.loadTerrain("nonexistent.tif"), std::exception);
  EXPECT_THROW(io::loadGeoTiffDem("nonexistent.tif"), std::exception);

  CoverageResult cr;
  cr.grid.cols = 1;
  cr.grid.rows = 1;
  cr.powerDbm = {-50.0};
  EXPECT_THROW(io::exportCoverageGeoTiff(cr, "out.tif"), std::exception);
}

#endif  // RFTRACE_HAVE_GDAL
