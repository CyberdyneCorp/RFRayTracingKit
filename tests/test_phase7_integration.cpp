#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cmath>

#include "rftrace/exporters/json_exporter.hpp"
#include "rftrace/rf/array.hpp"
#include "rftrace/rf/atmospheric.hpp"
#include "rftrace/rf/mimo.hpp"
#include "rftrace/simulator.hpp"

using namespace rftrace;

// antenna-arrays spec: "Array gain applied to a path" — a steered array's gain
// must actually appear in the per-path received power, not just in a helper.
TEST(Phase7Integration, ArrayGainEntersPathBudget) {
  const double f = 3.5e9;
  const double lambda = constants::c / f;

  auto makeScene = [&](bool withArray) {
    Scene s;
    Transmitter tx;
    tx.id = "tx";
    tx.position = {0, 0, 10};
    tx.frequencyHz = f;
    tx.powerDbm = 43.0;
    if (withArray)
      tx.array = rf::uniformLinearArray(4, 0.5 * lambda, f, {0, 1, 0}, 0.0);
    s.addTransmitter(tx);
    Receiver rx;
    rx.id = "rx";
    rx.position = {100, 0, 10};
    s.addReceiver(rx);
    return s;
  };

  SimulationSettings st;
  st.maxReflections = 0;
  const double pNo =
      Simulator(st).run(makeScene(false)).receiver("rx")->receivedPowerDbm;
  const double pArr =
      Simulator(st).run(makeScene(true)).receiver("rx")->receivedPowerDbm;

  // Broadside 4-element ULA steered to the path adds 10*log10(4) ≈ 6.02 dB.
  EXPECT_NEAR(pArr - pNo, 10.0 * std::log10(4.0), 0.1);
}

// results-export spec: "MIMO matrix export" — dims + complex entries + capacity.
TEST(Phase7Integration, MimoExportHasDimsEntriesAndCapacity) {
  Eigen::MatrixXcd h(2, 2);
  h << std::complex<double>(1.0, 0.0), std::complex<double>(0.0, 0.5),
      std::complex<double>(0.2, -0.1), std::complex<double>(0.8, 0.3);
  const double snr = 100.0;  // 20 dB

  const std::string js = io::mimoToJsonString(h, snr);
  const auto j = nlohmann::json::parse(js);
  EXPECT_EQ(j.at("rows").get<int>(), 2);
  EXPECT_EQ(j.at("cols").get<int>(), 2);
  ASSERT_TRUE(j.at("real").is_array() && j.at("real").size() == 2);
  ASSERT_TRUE(j.at("imag").is_array() && j.at("imag").size() == 2);
  EXPECT_NEAR(j.at("real").at(0).at(0).get<double>(), 1.0, 1e-12);
  EXPECT_NEAR(j.at("imag").at(0).at(1).get<double>(), 0.5, 1e-12);
  EXPECT_NEAR(j.at("capacity_bps_hz").get<double>(), rf::capacity(h, snr), 1e-9);
}

// atmospheric-attenuation: pin an absolute ITU-R P.838 reference value so a
// coefficient regression is caught (complements the trend-only tests).
TEST(Phase7Integration, RainAttenuationAbsoluteReference) {
  // ~30 GHz, 25 mm/h: order ~4-5 dB/km per P.838-3.
  const double g = rf::rainSpecificAttenuationDbPerKm(30e9, 25.0);
  EXPECT_GT(g, 3.5);
  EXPECT_LT(g, 6.0);
  // Negligible at 2.4 GHz.
  EXPECT_LT(rf::rainSpecificAttenuationDbPerKm(2.4e9, 25.0), 0.05);
}
