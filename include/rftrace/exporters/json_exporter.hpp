#pragma once

#include <string>

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

}  // namespace rftrace::io
