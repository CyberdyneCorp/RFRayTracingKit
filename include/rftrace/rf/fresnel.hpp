#pragma once

#include <cmath>
#include <complex>

#include "rftrace/math.hpp"

namespace rftrace::rf {

using Complex = std::complex<double>;

/// Fresnel polarization: TE = perpendicular (s), TM = parallel (p).
enum class FresnelPolarization { TE, TM };

/// Complex relative permittivity  εr − j·σ/(ω·ε0).
inline Complex complexPermittivity(double relativePermittivity,
                                   double conductivity, double frequencyHz) {
  constexpr double eps0 = 8.8541878128e-12;  // F/m
  const double imag =
      conductivity / (constants::two_pi * frequencyHz * eps0);
  return Complex(relativePermittivity, -imag);
}

/// Fresnel reflection coefficient for a wave in air incident on a medium of
/// complex permittivity `epsc`, at `incidenceAngleRad` measured from the surface
/// normal.
inline Complex fresnelReflectionCoefficient(Complex epsc,
                                            double incidenceAngleRad,
                                            FresnelPolarization pol) {
  const double ci = std::cos(incidenceAngleRad);
  const double si = std::sin(incidenceAngleRad);
  const Complex root = std::sqrt(epsc - Complex(si * si, 0.0));
  if (pol == FresnelPolarization::TE) {
    return (Complex(ci, 0.0) - root) / (Complex(ci, 0.0) + root);
  }
  return (epsc * ci - root) / (epsc * ci + root);
}

/// Magnitude |Γ| of the Fresnel reflection coefficient (0..1).
inline double reflectionCoefficientMagnitude(Complex epsc,
                                             double incidenceAngleRad,
                                             FresnelPolarization pol) {
  return std::abs(
      fresnelReflectionCoefficient(epsc, incidenceAngleRad, pol));
}

}  // namespace rftrace::rf
