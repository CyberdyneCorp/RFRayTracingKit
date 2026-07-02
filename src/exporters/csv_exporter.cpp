#include "rftrace/exporters/csv_exporter.hpp"

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

}  // namespace rftrace::io
