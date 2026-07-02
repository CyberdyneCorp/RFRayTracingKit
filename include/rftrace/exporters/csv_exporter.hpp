#pragma once

#include <string>

#include "rftrace/result.hpp"

namespace rftrace::io {

/// Per-receiver summary as a CSV string (header + one row per receiver).
std::string receiversToCsvString(const RFResult& result);

/// Write the per-receiver summary CSV to a file.
void exportReceiversCsv(const RFResult& result, const std::string& path);

}  // namespace rftrace::io
