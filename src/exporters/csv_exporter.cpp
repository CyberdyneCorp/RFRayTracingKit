#include "rftrace/exporters/csv_exporter.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rftrace::io {

std::string receiversToCsvString(const RFResult& result) {
  std::ostringstream os;
  os << "receiver_id,x,y,z,received_power_dbm,path_loss_db,delay_spread_ns,"
        "num_paths\n";
  for (const auto& rx : result.receivers) {
    os << rx.receiverId << ',' << rx.position.x() << ',' << rx.position.y()
       << ',' << rx.position.z() << ',';
    if (rx.hasSignal) {
      os << rx.receivedPowerDbm << ',' << rx.pathLossDb << ','
         << rx.delaySpreadNs;
    } else {
      // No-signal sentinel: empty power/loss/spread fields.
      os << ",,";
    }
    os << ',' << rx.paths.size() << '\n';
  }
  return os.str();
}

void exportReceiversCsv(const RFResult& result, const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write CSV to '" + path + "'");
  out << receiversToCsvString(result);
}

std::string coverageToCsvString(const CoverageResult& coverage) {
  std::ostringstream os;
  os << "row,col,x,y,power\n";
  const CoverageGrid& g = coverage.grid;
  for (int row = 0; row < g.rows; ++row) {
    for (int col = 0; col < g.cols; ++col) {
      const Vec3 c = g.cellCenter(row, col);
      const double p = coverage.powerDbm[row * g.cols + col];
      os << row << ',' << col << ',' << c.x() << ',' << c.y() << ',';
      if (std::isfinite(p))
        os << p;
      else
        os << "nan";  // documented no-signal sentinel
      os << '\n';
    }
  }
  return os.str();
}

void exportCoverageCsv(const CoverageResult& coverage, const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write coverage CSV to '" + path + "'");
  out << coverageToCsvString(coverage);
}

}  // namespace rftrace::io
