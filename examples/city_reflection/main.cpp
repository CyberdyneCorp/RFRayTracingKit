// city_reflection — a transmitter and receiver in front of a wall, producing a
// direct path plus a single specular reflection off the wall.
#include <iostream>

#include "rftrace/rftrace.hpp"

int main() {
  using namespace rftrace;

  Scene scene;
  scene.addMaterial(materials::preset("concrete"));

  // Vertical wall in the plane y = 100, spanning x∈[0,300], z∈[0,50].
  const Vec3 a{0, 100, 0}, b{300, 100, 0}, c{300, 100, 50}, d{0, 100, 50};
  scene.addMesh({Triangle{a, b, c}, Triangle{a, c, d}}, "concrete");

  Transmitter tx;
  tx.id = "tower_1";
  tx.position = {100.0, 20.0, 20.0};
  tx.frequencyHz = 3.5e9;
  tx.powerDbm = 43.0;
  scene.addTransmitter(tx);

  Receiver rx;
  rx.id = "rx_001";
  rx.position = {200.0, 20.0, 10.0};
  scene.addReceiver(rx);

  SimulationSettings settings;
  settings.maxReflections = 1;
  settings.simulationId = "city_reflection";

  Simulator sim(settings);
  const RFResult result = sim.run(scene);

  const ReceiverResult* r = result.receiver("rx_001");
  if (r == nullptr) {
    std::cout << "FAIL: rx_001 missing\n";
    return 1;
  }

  int reflections = 0;
  for (const RFPath& p : r->paths)
    if (p.type == PathType::Reflection) ++reflections;

  io::exportResultJson(result, "city_reflection.json");

  std::cout << "rx_001 paths=" << r->paths.size()
            << " (reflection paths=" << reflections
            << ") aggregate_power=" << r->receivedPowerDbm << " dBm\n";

  if (reflections < 1) {
    std::cout << "FAIL: expected at least one reflection path\n";
    return 1;
  }
  return 0;
}
