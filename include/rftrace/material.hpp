#pragma once

#include <string>
#include <string_view>

namespace rftrace {

/// Electromagnetic description of a surface material.
///
/// When `relativePermittivity > 1` the RF layer derives reflection loss from the
/// Fresnel equations; otherwise it falls back to the constant `reflectionLossDb`.
struct Material {
  std::string name = "default";
  double relativePermittivity = 1.0;  ///< εr (real part)
  double conductivity = 0.0;          ///< σ in S/m
  double roughness = 0.0;             ///< RMS surface roughness in metres
  double penetrationLossDb = 0.0;     ///< transmission loss through the surface
  double reflectionLossDb = 0.0;      ///< constant-loss fallback for reflection

  /// True when Fresnel-based reflection can be computed from εr/σ.
  bool hasElectricalParameters() const { return relativePermittivity > 1.0; }
};

namespace materials {

/// True if `name` (case-sensitive) is a known built-in preset.
bool hasPreset(std::string_view name);

/// Return a built-in material preset. Falls back to a neutral default (with the
/// requested name) when the preset is unknown; check `hasPreset` first if that
/// matters. Presets: concrete, brick, glass, metal, wood, water, vegetation,
/// asphalt, soil.
Material preset(std::string_view name);

}  // namespace materials
}  // namespace rftrace
