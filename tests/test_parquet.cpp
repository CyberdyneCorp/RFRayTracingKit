// Per-receiver Parquet export tests.
//
// The Arrow-backed assertions are compiled only when RFTRACE_HAVE_PARQUET is
// defined (RFTRACE_ENABLE_PARQUET=ON). In the default build the file still
// contributes a test that verifies the graceful-degradation contract:
// parquetAvailable() is false and the entry point throws.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "rftrace/exporters/parquet_exporter.hpp"
#include "rftrace/result.hpp"

using namespace rftrace;

namespace {
RFResult sampleResult() {
  RFResult r;
  r.simulationId = "sim";
  ReceiverResult a;
  a.receiverId = "rx0";
  a.position = {1.0, 2.0, 3.0};
  a.hasSignal = true;
  a.receivedPowerDbm = -70.0;
  a.pathLossDb = 110.0;
  a.delaySpreadNs = 12.5;
  ReceiverResult b;
  b.receiverId = "rx1";
  b.position = {4.0, 5.0, 6.0};
  b.hasSignal = false;
  b.receivedPowerDbm = -95.0;
  b.pathLossDb = 135.0;
  b.delaySpreadNs = 30.0;
  r.receivers = {a, b};
  return r;
}
}  // namespace

#if RFTRACE_HAVE_PARQUET

#include <memory>
#include <vector>

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>

TEST(Parquet, Availability) { EXPECT_TRUE(io::parquetAvailable()); }

TEST(Parquet, WritesAndRereadsSchemaAndRows) {
  const RFResult r = sampleResult();
  const std::string path = ::testing::TempDir() + "/rftrace_receivers.parquet";
  io::exportReceiversParquet(r, path);

  auto infileResult = arrow::io::ReadableFile::Open(path);
  ASSERT_TRUE(infileResult.ok()) << infileResult.status().ToString();
  std::shared_ptr<arrow::io::ReadableFile> infile = *infileResult;

  auto readerResult =
      parquet::arrow::OpenFile(infile, arrow::default_memory_pool());
  ASSERT_TRUE(readerResult.ok()) << readerResult.status().ToString();
  std::unique_ptr<parquet::arrow::FileReader> reader =
      std::move(*readerResult);

  auto tableResult = reader->ReadTable();
  ASSERT_TRUE(tableResult.ok()) << tableResult.status().ToString();
  std::shared_ptr<arrow::Table> table = *tableResult;

  EXPECT_EQ(table->num_rows(), static_cast<std::int64_t>(r.receivers.size()));
  EXPECT_EQ(table->num_columns(), 7);

  const std::vector<std::string> expected = {
      "id", "x", "y", "z", "received_power_dbm", "path_loss_db",
      "delay_spread_ns"};
  const auto schema = table->schema();
  ASSERT_EQ(schema->num_fields(), static_cast<int>(expected.size()));
  for (int i = 0; i < schema->num_fields(); ++i)
    EXPECT_EQ(schema->field(i)->name(), expected[i]);
}

#else  // !RFTRACE_HAVE_PARQUET

TEST(Parquet, GracefulWithoutParquet) {
  EXPECT_FALSE(io::parquetAvailable());
  EXPECT_THROW(io::exportReceiversParquet(sampleResult(), "out.parquet"),
               std::exception);
}

#endif  // RFTRACE_HAVE_PARQUET
