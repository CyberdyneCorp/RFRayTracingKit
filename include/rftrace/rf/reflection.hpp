#pragma once

#include <algorithm>
#include <cmath>

#include "rftrace/antenna.hpp"
#include "rftrace/material.hpp"
#include "rftrace/rf/fresnel.hpp"

namespace rftrace::rf {

/// Map a wave polarization to the Fresnel TE/TM convention used at a surface.
/// Horizontal maps to TE (perpendicular); everything else to TM (parallel).
inline FresnelPolarization toFresnel(Polarization pol) {
  return pol == Polarization::Horizontal ? FresnelPolarization::TE
                                         : FresnelPolarization::TM;
}

/// Reflection loss (dB) at a surface. Uses the Fresnel equations when the
/// material carries electrical parameters, otherwise the material's constant
/// `reflectionLossDb`. `incidenceAngleRad` is measured from the surface normal.
inline double reflectionLossDb(const Material& material,
                               double incidenceAngleRad, Polarization pol,
                               double frequencyHz) {
  if (!material.hasElectricalParameters()) return material.reflectionLossDb;

  const Complex epsc = complexPermittivity(
      material.relativePermittivity, material.conductivity, frequencyHz);
  const double mag = reflectionCoefficientMagnitude(
      epsc, std::clamp(incidenceAngleRad, 0.0, constants::pi / 2.0),
      toFresnel(pol));
  if (mag <= 1e-6) return 200.0;  // near-total absorption sentinel
  return -20.0 * std::log10(mag);
}

}  // namespace rftrace::rf
