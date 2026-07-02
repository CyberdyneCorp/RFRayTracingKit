#pragma once

#include <string>

#include "rftrace/coverage.hpp"
#include "rftrace/result.hpp"

namespace rftrace::io {

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

}  // namespace rftrace::io
