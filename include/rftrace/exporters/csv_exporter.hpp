#pragma once

#include <string>

#include "rftrace/coverage.hpp"
#include "rftrace/result.hpp"

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

}  // namespace rftrace::io
