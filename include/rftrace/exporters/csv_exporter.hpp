#pragma once

#include <string>

#include "rftrace/coverage.hpp"
#include "rftrace/result.hpp"
#include "rftrace/route.hpp"

namespace rftrace::io {

/// Per-receiver summary as a CSV string (header + one row per receiver).
std::string receiversToCsvString(const RFResult& result);

/// Write the per-receiver summary CSV to a file.
void exportReceiversCsv(const RFResult& result, const std::string& path);

/// Coverage as a long CSV table: header `row,col,x,y,power`, one row per cell.
/// No-signal cells carry `nan` in the power column.
std::string coverageToCsvString(const CoverageResult& coverage);

/// Write the coverage long-table CSV to a file.
void exportCoverageCsv(const CoverageResult& coverage, const std::string& path);

/// Route (drive-test) result as CSV: one row per sample in route order. Columns
/// `index,distance_m,x,y,z,received_power_dbm,path_loss_db,delay_spread_ns`
/// (+ `serving_transmitter_id,sinr_db,interference_power_dbm` when SINR is set).
/// No-signal samples carry empty power/loss/spread fields.
std::string routeToCsvString(const RouteResult& route);

/// Write the route drive-test CSV to a file.
void exportRouteCsv(const RouteResult& route, const std::string& path);

}  // namespace rftrace::io
