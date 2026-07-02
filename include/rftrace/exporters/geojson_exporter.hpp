#pragma once

#include <string>

#include "rftrace/coverage.hpp"
#include "rftrace/result.hpp"

namespace rftrace::io {

/// Receivers as a GeoJSON FeatureCollection of Point features.
std::string receiversToGeoJsonString(const RFResult& result);
void exportReceiversGeoJson(const RFResult& result, const std::string& path);

/// Ray paths as a GeoJSON FeatureCollection of LineString features.
std::string pathsToGeoJsonString(const RFResult& result);
void exportPathsGeoJson(const RFResult& result, const std::string& path);

/// Coverage cells as a GeoJSON FeatureCollection of Polygon features (covered
/// cells only).
std::string coverageToGeoJsonString(const CoverageResult& coverage);
void exportCoverageGeoJson(const CoverageResult& coverage,
                           const std::string& path);

}  // namespace rftrace::io
