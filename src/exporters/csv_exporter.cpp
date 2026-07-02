#include "rftrace/exporters/csv_exporter.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rftrace::io {

std::string receiversToCsvString(const RFResult& result) {
  // SINR columns are appended only when at least one receiver carries a
  // serving-cell assignment, so default results keep their archived schema.
  bool hasSinr = false;
  for (const auto& rx : result.receivers)
    if (!rx.servingTransmitterId.empty()) {
      hasSinr = true;
      break;
    }

  std::ostringstream os;
  os << "receiver_id,x,y,z,received_power_dbm,path_loss_db,delay_spread_ns,"
        "num_paths";
  if (hasSinr) os << ",serving_transmitter_id,sinr_db,interference_power_dbm";
  os << '\n';

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
    os << ',' << rx.paths.size();
    if (hasSinr) {
      os << ',' << rx.servingTransmitterId << ',';
      if (std::isfinite(rx.sinrDb)) os << rx.sinrDb;
      os << ',';
      if (std::isfinite(rx.interferencePowerDbm)) os << rx.interferencePowerDbm;
    }
    os << '\n';
  }
  return os.str();
}

void exportReceiversCsv(const RFResult& result, const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write CSV to '" + path + "'");
  out << receiversToCsvString(result);
}

std::string coverageToCsvString(const CoverageResult& coverage) {
  const bool hasSinr = !coverage.sinrDb.empty();
  std::ostringstream os;
  os << "row,col,x,y,power";
  if (hasSinr) os << ",sinr_db";
  os << '\n';
  const CoverageGrid& g = coverage.grid;
  for (int row = 0; row < g.rows; ++row) {
    for (int col = 0; col < g.cols; ++col) {
      const int idx = row * g.cols + col;
      const Vec3 c = g.cellCenter(row, col);
      const double p = coverage.powerDbm[idx];
      os << row << ',' << col << ',' << c.x() << ',' << c.y() << ',';
      if (std::isfinite(p))
        os << p;
      else
        os << "nan";  // documented no-signal sentinel
      if (hasSinr) {
        const double s = coverage.sinrDb[idx];
        os << ',';
        if (std::isfinite(s))
          os << s;
        else
          os << "nan";
      }
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

std::string routeToCsvString(const RouteResult& route) {
  // SINR columns are appended only when at least one sample carries a
  // serving-cell assignment, mirroring the receiver-summary CSV schema.
  bool hasSinr = false;
  for (const auto& s : route.samples)
    if (!s.servingTransmitterId.empty()) {
      hasSinr = true;
      break;
    }

  std::ostringstream os;
  os << "index,distance_m,x,y,z,received_power_dbm,path_loss_db,"
        "delay_spread_ns";
  if (hasSinr) os << ",serving_transmitter_id,sinr_db,interference_power_dbm";
  os << '\n';

  for (const auto& s : route.samples) {
    os << s.index << ',' << s.distanceMeters << ',' << s.position.x() << ','
       << s.position.y() << ',' << s.position.z() << ',';
    if (s.hasSignal) {
      os << s.receivedPowerDbm << ',' << s.pathLossDb << ','
         << s.delaySpreadNs;
    } else {
      // No-signal sentinel: empty power/loss/spread fields.
      os << ",,";
    }
    if (hasSinr) {
      os << ',' << s.servingTransmitterId << ',';
      if (std::isfinite(s.sinrDb)) os << s.sinrDb;
      os << ',';
      if (std::isfinite(s.interferencePowerDbm)) os << s.interferencePowerDbm;
    }
    os << '\n';
  }
  return os.str();
}

void exportRouteCsv(const RouteResult& route, const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write route CSV to '" + path + "'");
  out << routeToCsvString(route);
}

}  // namespace rftrace::io
