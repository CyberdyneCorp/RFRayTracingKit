#pragma once

#include <string>

#include "rftrace/result.hpp"

namespace rftrace::io {

/// True only in an Arrow/Parquet-enabled build (RFTRACE_ENABLE_PARQUET=ON). When
/// false the Parquet export entry point is still declared but throws a clear
/// "built without Parquet" runtime error.
bool parquetAvailable();

/// Export the per-receiver result table to a Parquet file (Apache Arrow). The
/// table has columns `id`, `x`, `y`, `z`, `received_power_dbm`, `path_loss_db`,
/// and `delay_spread_ns`, one row per receiver.
///
/// Throws a runtime error on I/O failure, and — in a build without Arrow/Parquet
/// — a runtime error stating the library was built without Parquet.
void exportReceiversParquet(const RFResult& result, const std::string& path);

}  // namespace rftrace::io
