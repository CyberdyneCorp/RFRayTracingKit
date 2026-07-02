// simple_los — one transmitter, one receiver, unobstructed line of sight.
// Demonstrates the minimal public API and JSON/CSV export.
#include <iostream>

#include "rftrace/rftrace.hpp"

int main() {
  using namespace rftrace;

  Scene scene;

  Transmitter tx;
  tx.id = "tower_1";
  tx.position = {120.0, 80.0, 35.0};  // Z is height
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);

  Receiver rx;
  rx.id = "rx_001";
  rx.position = {300.0, 180.0, 1.5};
  scene.addReceiver(rx);

  SimulationSettings settings;
  settings.maxReflections = 0;  // LOS only
  settings.simulationId = "simple_los";

  Simulator sim(settings);
  const RFResult result = sim.run(scene);

  const ReceiverResult* r = result.receiver("rx_001");
  if (r == nullptr || !r->hasSignal) {
    std::cout << "FAIL: no signal at rx_001\n";
    return 1;
  }

  io::exportResultJson(result, "simple_los.json");
  io::exportReceiversCsv(result, "simple_los.csv");

  std::cout << "rx_001 received_power=" << r->receivedPowerDbm
            << " dBm  path_loss=" << r->pathLossDb << " dB  paths="
            << r->paths.size() << "  (exported simple_los.json/.csv)\n";
  return 0;
}
