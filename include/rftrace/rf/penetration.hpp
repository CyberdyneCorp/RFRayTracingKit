#pragma once

#include "rftrace/material.hpp"

namespace rftrace::rf {

/// Transmission (penetration) loss in dB for passing through a material.
inline double penetrationLossDb(const Material& material) {
  return material.penetrationLossDb;
}

}  // namespace rftrace::rf
