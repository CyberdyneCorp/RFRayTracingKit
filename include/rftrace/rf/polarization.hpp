#pragma once

// Polarization (D3): a Jones-vector model of a path's polarization state, the
// polarization-mismatch loss between two antennas, and depolarizing reflection.
//
// The Jones vector is expressed in a fixed transverse basis (component 0 =
// Vertical, component 1 = Horizontal). Canonical antenna states:
//   Vertical   = (1, 0)
//   Horizontal = (0, 1)
//   RHCP       = (1, -j) / sqrt(2)
//   LHCP       = (1, +j) / sqrt(2)
//
// Mismatch uses the normalized Hermitian inner product; co-polar antennas incur
// 0 dB, a 45 deg linear offset ~3.01 dB, and orthogonal states a large value
// clamped to a documented sentinel so the budget never sees +inf/NaN.
//
// Depolarizing reflection maps the Vertical component through the TM (parallel)
// Fresnel coefficient and the Horizontal component through the TE (perpendicular)
// coefficient, matching the TE/TM convention in reflection.hpp.

#include <algorithm>
#include <cmath>
#include <complex>

#include "rftrace/antenna.hpp"
#include "rftrace/rf/fresnel.hpp"

namespace rftrace::rf {

/// Upper bound (dB) reported for (near-)orthogonal polarizations. Orthogonal
/// states would give -10*log10(0) = +inf; the loss is clamped to this sentinel.
inline constexpr double kMaxPolarizationMismatchDb = 100.0;

/// A two-component complex Jones vector in the transverse (V, H) basis.
struct Jones {
  Complex v{1.0, 0.0};  ///< Vertical component (maps to TM at a surface)
  Complex h{0.0, 0.0};  ///< Horizontal component (maps to TE at a surface)

  /// Squared magnitude |J|^2 = |v|^2 + |h|^2.
  double normSquared() const {
    return std::norm(v) + std::norm(h);
  }
};

/// Hermitian inner product <a, b> = conj(a) . b.
inline Complex jonesInner(const Jones& a, const Jones& b) {
  return std::conj(a.v) * b.v + std::conj(a.h) * b.h;
}

/// Canonical Jones vector for a polarization enum. `None` is treated as the
/// Vertical reference (mismatch against `None` is short-circuited to 0 dB by the
/// enum overload of `polarizationMismatchDb`).
inline Jones jonesFor(Polarization pol) {
  constexpr double invSqrt2 = 0.7071067811865475244;  // 1/sqrt(2)
  switch (pol) {
    case Polarization::Vertical:
      return Jones{Complex(1.0, 0.0), Complex(0.0, 0.0)};
    case Polarization::Horizontal:
      return Jones{Complex(0.0, 0.0), Complex(1.0, 0.0)};
    case Polarization::RHCP:
      return Jones{Complex(invSqrt2, 0.0), Complex(0.0, -invSqrt2)};
    case Polarization::LHCP:
      return Jones{Complex(invSqrt2, 0.0), Complex(0.0, invSqrt2)};
    case Polarization::None:
      return Jones{Complex(1.0, 0.0), Complex(0.0, 0.0)};
  }
  return Jones{Complex(1.0, 0.0), Complex(0.0, 0.0)};
}

/// Polarization mismatch loss (dB) between a transmit and receive Jones vector:
///   L = -10 * log10( |<rx, tx>|^2 / (|rx|^2 |tx|^2) ).
/// Co-polar -> 0 dB, 45 deg linear -> ~3.01 dB, orthogonal -> clamped sentinel.
inline double polarizationMismatchDb(const Jones& tx, const Jones& rx) {
  const double denom = tx.normSquared() * rx.normSquared();
  if (denom <= 0.0) return kMaxPolarizationMismatchDb;
  const double ratio = std::norm(jonesInner(rx, tx)) / denom;
  if (ratio <= 0.0) return kMaxPolarizationMismatchDb;
  const double loss = -10.0 * std::log10(ratio);
  return std::clamp(loss, 0.0, kMaxPolarizationMismatchDb);
}

/// Convenience overload from the polarization enum. When either side is `None`,
/// polarization is disabled and the mismatch is 0 dB.
inline double polarizationMismatchDb(Polarization tx, Polarization rx) {
  if (tx == Polarization::None || rx == Polarization::None) return 0.0;
  return polarizationMismatchDb(jonesFor(tx), jonesFor(rx));
}

/// Depolarize a Jones vector on reflection: scale the Vertical (TM/parallel)
/// component by `tmCoeff` and the Horizontal (TE/perpendicular) component by
/// `teCoeff`, the material's complex Fresnel reflection coefficients. When the
/// two coefficients differ in magnitude or phase the polarization is rotated /
/// ellipticized accordingly.
inline Jones reflectDepolarize(const Jones& j, Complex teCoeff, Complex tmCoeff) {
  return Jones{tmCoeff * j.v, teCoeff * j.h};
}

}  // namespace rftrace::rf
