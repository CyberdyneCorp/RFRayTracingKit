#pragma once

#include <Eigen/Dense>
#include <string>

#include "rftrace/coverage.hpp"
#include "rftrace/result.hpp"
#include "rftrace/route.hpp"

namespace rftrace::io {

/// Serialize a MIMO channel matrix to JSON: dimensions, real/imag entries, and
/// the equal-power narrowband capacity (bits/s/Hz) at the given linear SNR.
std::string mimoToJsonString(const Eigen::MatrixXcd& channel, double snrLinear);

/// Write the MIMO channel matrix + capacity to a JSON file.
void exportMimoJson(const Eigen::MatrixXcd& channel, double snrLinear,
                    const std::string& path);

/// Serialize a result to a JSON string (schema-conformant with the spec).
std::string resultToJsonString(const RFResult& result);

/// Write a result to a JSON file.
void exportResultJson(const RFResult& result, const std::string& path);

/// Parse a result back from a JSON string (round-trip loader).
RFResult resultFromJsonString(const std::string& text);

/// Load a result from a JSON file.
RFResult loadResultJson(const std::string& path);

/// Serialize a coverage result to JSON (grid metadata + row-major arrays).
std::string coverageToJsonString(const CoverageResult& coverage);

/// Write a coverage result to a JSON file.
void exportCoverageJson(const CoverageResult& coverage, const std::string& path);

/// Serialize a route (drive-test) result to JSON: metadata plus an ordered
/// `samples` array (index, distance, position, metrics, + SINR when set).
std::string routeToJsonString(const RouteResult& route);

/// Write a route result to a JSON file.
void exportRouteJson(const RouteResult& route, const std::string& path);

}  // namespace rftrace::io
