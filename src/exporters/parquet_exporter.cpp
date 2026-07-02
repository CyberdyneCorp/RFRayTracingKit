#include "rftrace/exporters/parquet_exporter.hpp"

#include <stdexcept>
#include <string>

#if RFTRACE_HAVE_PARQUET
#include <memory>
#include <vector>

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/writer.h>
#endif

namespace rftrace::io {

#if RFTRACE_HAVE_PARQUET

namespace {
/// Throw a runtime error carrying an Arrow status message.
void check(const arrow::Status& st, const std::string& what) {
  if (!st.ok())
    throw std::runtime_error("Parquet export: " + what + ": " + st.ToString());
}
}  // namespace

bool parquetAvailable() { return true; }

void exportReceiversParquet(const RFResult& result, const std::string& path) {
  arrow::StringBuilder idB;
  arrow::DoubleBuilder xB, yB, zB, powerB, lossB, delayB;

  for (const auto& rx : result.receivers) {
    check(idB.Append(rx.receiverId), "append id");
    check(xB.Append(rx.position.x()), "append x");
    check(yB.Append(rx.position.y()), "append y");
    check(zB.Append(rx.position.z()), "append z");
    check(powerB.Append(rx.receivedPowerDbm), "append received_power_dbm");
    check(lossB.Append(rx.pathLossDb), "append path_loss_db");
    check(delayB.Append(rx.delaySpreadNs), "append delay_spread_ns");
  }

  std::shared_ptr<arrow::Array> idA, xA, yA, zA, powerA, lossA, delayA;
  check(idB.Finish(&idA), "finish id");
  check(xB.Finish(&xA), "finish x");
  check(yB.Finish(&yA), "finish y");
  check(zB.Finish(&zA), "finish z");
  check(powerB.Finish(&powerA), "finish received_power_dbm");
  check(lossB.Finish(&lossA), "finish path_loss_db");
  check(delayB.Finish(&delayA), "finish delay_spread_ns");

  auto schema = arrow::schema({
      arrow::field("id", arrow::utf8()),
      arrow::field("x", arrow::float64()),
      arrow::field("y", arrow::float64()),
      arrow::field("z", arrow::float64()),
      arrow::field("received_power_dbm", arrow::float64()),
      arrow::field("path_loss_db", arrow::float64()),
      arrow::field("delay_spread_ns", arrow::float64()),
  });
  auto table = arrow::Table::Make(
      schema, {idA, xA, yA, zA, powerA, lossA, delayA});

  auto outfileResult = arrow::io::FileOutputStream::Open(path);
  if (!outfileResult.ok())
    throw std::runtime_error("Parquet export: cannot open '" + path +
                             "': " + outfileResult.status().ToString());
  std::shared_ptr<arrow::io::FileOutputStream> outfile = *outfileResult;

  check(parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), outfile,
                                   /*chunk_size=*/1024),
        "write table");
  check(outfile->Close(), "close file");
}

#else  // !RFTRACE_HAVE_PARQUET

bool parquetAvailable() { return false; }

void exportReceiversParquet(const RFResult& /*result*/,
                            const std::string& /*path*/) {
  throw std::runtime_error(
      "Parquet export requires Apache Arrow/Parquet, but the library was built "
      "without Parquet (configure with -DRFTRACE_ENABLE_PARQUET=ON)");
}

#endif  // RFTRACE_HAVE_PARQUET

}  // namespace rftrace::io
