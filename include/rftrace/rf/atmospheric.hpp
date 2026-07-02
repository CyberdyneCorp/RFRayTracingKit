#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "rftrace/math.hpp"

// Atmospheric specific attenuation models (Phase 7, R5 companion):
//   * Rain     — ITU-R P.838-3 power law  gamma_R = k * R^alpha  (dB/km)
//   * Gaseous  — ITU-R P.676 simplified oxygen + water-vapour model (dB/km)
//
// Both return a *specific* attenuation (dB per km); the per-path budget
// multiplies by the path length in km. All functions are pure, header-only
// inline formulas (same style as the other rf/*.hpp modules) and return 0 for
// degenerate (non-positive) inputs so they never inject NaN/inf into a budget.

namespace rftrace::rf {

namespace detail {

/// One row of the ITU-R P.838-3 regression coefficient table.
struct RainCoeffRow {
  double freqGHz;
  double kH;
  double alphaH;
  double kV;
  double alphaV;
};

/// ITU-R P.838-3 coefficients (subset, 1–100 GHz). k is interpolated on a
/// log(freq)–log(k) scale, alpha on a log(freq)–linear scale, per the
/// recommendation's interpolation note.
inline constexpr std::array<RainCoeffRow, 27> kRainTable{{
    {1.0, 0.0000259, 0.9691, 0.0000308, 0.8592},
    {1.5, 0.0000443, 1.0185, 0.0000574, 0.8957},
    {2.0, 0.0000847, 1.0664, 0.0000998, 0.9490},
    {2.5, 0.0001321, 1.1209, 0.0001464, 1.0085},
    {3.0, 0.0001390, 1.2322, 0.0001942, 1.0688},
    {3.5, 0.0001155, 1.4189, 0.0002346, 1.1387},
    {4.0, 0.0001071, 1.6009, 0.0002461, 1.2476},
    {4.5, 0.0001340, 1.6948, 0.0002347, 1.3987},
    {5.0, 0.0002162, 1.6969, 0.0002428, 1.5317},
    {6.0, 0.0003909, 1.6499, 0.0003115, 1.5820},
    {7.0, 0.0007056, 1.5900, 0.0004878, 1.5728},
    {8.0, 0.001915, 1.4810, 0.001425, 1.4745},
    {10.0, 0.01217, 1.2571, 0.01129, 1.2156},
    {12.0, 0.02386, 1.1825, 0.02455, 1.1216},
    {15.0, 0.04481, 1.1233, 0.05008, 1.0440},
    {20.0, 0.09164, 1.0568, 0.09611, 0.9847},
    {25.0, 0.1571, 0.9991, 0.1533, 0.9491},
    {30.0, 0.2403, 0.9485, 0.2291, 0.9129},
    {35.0, 0.3374, 0.9047, 0.3224, 0.8761},
    {40.0, 0.4431, 0.8673, 0.4274, 0.8421},
    {45.0, 0.5521, 0.8355, 0.5375, 0.8123},
    {50.0, 0.6600, 0.8084, 0.6472, 0.7871},
    {60.0, 0.8606, 0.7656, 0.8515, 0.7486},
    {70.0, 1.0315, 0.7345, 1.0253, 0.7215},
    {80.0, 1.1704, 0.7091, 1.1668, 0.7021},
    {90.0, 1.2807, 0.6857, 1.2795, 0.6829},
    {100.0, 1.3671, 0.6600, 1.3680, 0.6591},
}};

/// Interpolated P.838 coefficients (kH, alphaH, kV, alphaV) at `freqGHz`,
/// clamped to the table's frequency span.
inline RainCoeffRow interpolateRainCoeffs(double freqGHz) {
  const auto& t = kRainTable;
  const std::size_t n = t.size();
  if (freqGHz <= t.front().freqGHz) return t.front();
  if (freqGHz >= t.back().freqGHz) return t.back();

  std::size_t hi = 1;
  while (hi < n && t[hi].freqGHz < freqGHz) ++hi;
  const RainCoeffRow& lo = t[hi - 1];
  const RainCoeffRow& up = t[hi];

  // log-frequency interpolation fraction.
  const double lf = std::log(freqGHz / lo.freqGHz) /
                    std::log(up.freqGHz / lo.freqGHz);

  auto logInterp = [&](double a, double b) {
    return std::exp(std::log(a) + lf * (std::log(b) - std::log(a)));
  };
  auto linInterp = [&](double a, double b) { return a + lf * (b - a); };

  return RainCoeffRow{freqGHz, logInterp(lo.kH, up.kH),
                      linInterp(lo.alphaH, up.alphaH), logInterp(lo.kV, up.kV),
                      linInterp(lo.alphaV, up.alphaV)};
}

}  // namespace detail

/// Rain specific attenuation gamma_R = k · R^alpha in dB/km (ITU-R P.838-3),
/// with k/alpha interpolated for `frequencyHz`. Polarization is handled via the
/// linear-polarization tilt angle `polTiltDeg` (0 = horizontal, 90 = vertical)
/// and the path elevation `pathElevationDeg`:
///   k = [kH + kV + (kH − kV)·cos²θ·cos2τ] / 2
///   α = [kH·αH + kV·αV + (kH·αH − kV·αV)·cos²θ·cos2τ] / (2k)
/// The default τ = 45° (cos2τ = 0) yields the polarization-averaged value.
/// Returns 0 for a non-positive rain rate or frequency.
inline double rainSpecificAttenuationDbPerKm(double frequencyHz,
                                             double rainRateMmPerHr,
                                             double polTiltDeg = 45.0,
                                             double pathElevationDeg = 0.0) {
  if (rainRateMmPerHr <= 0.0 || frequencyHz <= 0.0) return 0.0;

  const double freqGHz = frequencyHz / 1e9;
  const detail::RainCoeffRow c = detail::interpolateRainCoeffs(freqGHz);

  const double tau = polTiltDeg * constants::pi / 180.0;
  const double theta = pathElevationDeg * constants::pi / 180.0;
  const double cos2Theta = std::cos(theta) * std::cos(theta);
  const double cos2Tau = std::cos(2.0 * tau);
  const double w = cos2Theta * cos2Tau;

  const double k = (c.kH + c.kV + (c.kH - c.kV) * w) / 2.0;
  const double num =
      c.kH * c.alphaH + c.kV * c.alphaV + (c.kH * c.alphaH - c.kV * c.alphaV) * w;
  const double alpha = (k > 0.0) ? num / (2.0 * k) : 0.0;

  return k * std::pow(rainRateMmPerHr, alpha);
}

/// Approximate gaseous (dry-air oxygen + water-vapour) specific attenuation in
/// dB/km, ITU-R P.676 Annex 2 simplified form (valid to ~57 GHz for oxygen and
/// ~350 GHz for water vapour). Parameters describe the local atmosphere:
///   `pressureHpa`      total (dry-air) pressure in hPa,
///   `temperatureC`     air temperature in °C,
///   `waterVapourDensity` ρ in g/m³.
/// Defaults are the P.676 reference standard atmosphere at sea level
/// (1013 hPa, 15 °C, 7.5 g/m³). Returns 0 for a non-positive frequency.
inline double gaseousSpecificAttenuationDbPerKm(double frequencyHz,
                                                double pressureHpa = 1013.0,
                                                double temperatureC = 15.0,
                                                double waterVapourDensity = 7.5) {
  if (frequencyHz <= 0.0) return 0.0;

  const double f = frequencyHz / 1e9;  // GHz
  const double rp = pressureHpa / 1013.0;
  const double rt = 288.0 / (273.0 + temperatureC);
  const double rho = waterVapourDensity;

  // Oxygen (dry air).
  const double gammaO =
      (7.19e-3 + 6.09 / (f * f + 0.227) +
       4.81 / ((f - 57.0) * (f - 57.0) + 1.50)) *
      f * f * rp * rp * std::pow(rt, 3.0) * 1e-3;

  // Water vapour.
  const double gammaW =
      (0.050 + 0.0021 * rho + 3.6 / ((f - 22.2) * (f - 22.2) + 8.5) +
       10.6 / ((f - 183.3) * (f - 183.3) + 9.0) +
       8.9 / ((f - 325.4) * (f - 325.4) + 26.3)) *
      f * f * rho * rp * std::pow(rt, 2.5) * 1e-4;

  return gammaO + gammaW;
}

}  // namespace rftrace::rf
