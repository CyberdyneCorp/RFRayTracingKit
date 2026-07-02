#pragma once

// Uniform Theory of Diffraction (UTD) — Kouyoumjian–Pathak wedge diffraction
// (D2). Header-only, backend-agnostic, single concern. Time convention is
// e^{-jωt}: outgoing waves are e^{+jkr} and the Fresnel transition function
// carries the +j / e^{jx} factors below. This is offered as an ALTERNATIVE
// diffraction model; the ITU-R knife-edge (diffraction.hpp) remains the default.
//
// References:
//   Kouyoumjian & Pathak, "A uniform geometrical theory of diffraction for an
//   edge in a perfectly conducting surface", Proc. IEEE 62(11), 1974.
//   McNamara, Pistorius & Malherbe, "Introduction to the Uniform Geometrical
//   Theory of Diffraction", Artech House, 1990 (chapter 4).

#include <cmath>
#include <complex>

#include "rftrace/math.hpp"

namespace rftrace::rf {

using UtdComplex = std::complex<double>;

namespace detail {

/// Faddeeva function w(z) = e^{-z²}·erfc(-j·z), valid for the upper half-plane
/// (Im z ≥ 0). Small |z|: the everywhere-convergent Taylor series of erfc.
/// Large |z|: the Gautschi continued fraction (evaluated with modified Lentz),
/// which is accurate — not merely asymptotic — throughout the transition region.
inline UtdComplex faddeevaW(UtdComplex z) {
  constexpr double invSqrtPi = 0.5641895835477563;  // 1/√π
  const double absZ = std::abs(z);

  if (absZ < 2.0) {
    // erfc(-jz) = 1 + (2j/√π)·Σ_{n≥0} z^{2n+1} / (n!·(2n+1)); w = e^{-z²}·erfc.
    const UtdComplex z2 = z * z;
    UtdComplex term = z;  // n = 0
    UtdComplex sum = term;
    for (int k = 1; k < 200; ++k) {
      // term_k / term_{k-1} = z²·(2k-1) / (k·(2k+1))
      term *= z2 * ((2.0 * k - 1.0) / (k * (2.0 * k + 1.0)));
      sum += term;
      if (std::abs(term) <= 1e-16 * std::abs(sum)) break;
    }
    const UtdComplex erfc = 1.0 + UtdComplex(0.0, 2.0 * invSqrtPi) * sum;
    return std::exp(-z2) * erfc;
  }

  // Continued fraction  CF = z − (1/2)/(z − 1/(z − (3/2)/(z − 2/(z − …)))),
  // with w(z) = (j/√π)/CF. Terms a_k = −k/2, b_k = z (k ≥ 1); b0 = z.
  constexpr double tiny = 1e-300;
  UtdComplex f = z;  // b0
  UtdComplex C = f;
  UtdComplex D = 0.0;
  for (int k = 1; k < 400; ++k) {
    const double a = -0.5 * k;
    D = z + a * D;
    if (std::abs(D) < tiny) D = tiny;
    C = z + a / C;
    if (std::abs(C) < tiny) C = tiny;
    D = 1.0 / D;
    const UtdComplex delta = C * D;
    f *= delta;
    if (std::abs(delta - 1.0) < 1e-15) break;
  }
  return UtdComplex(0.0, invSqrtPi) / f;
}

}  // namespace detail

/// Kouyoumjian–Pathak Fresnel transition function
///   F(x) = 2j·√x·e^{jx}·∫_{√x}^∞ e^{-jτ²} dτ,
/// evaluated in closed form as F(x) = √(πx)·e^{jπ/4}·w(√x·e^{j3π/4}) where w is
/// the Faddeeva function. The UTD argument x is real and non-negative; the
/// complex signature is honored (any Im x is carried through the closed form,
/// accurate for the upper-half-plane argument the formula maps to).
///
/// Limits: F(x) → 1 as x → ∞ (deep in the lit/shadow region); for x → 0,
/// F(x) → √(πx)·e^{j(π/4+x)} to leading order, so |F| → √(πx) and arg F → π/4.
inline UtdComplex utdTransition(UtdComplex x) {
  const UtdComplex e45 = std::exp(UtdComplex(0.0, constants::pi / 4.0));
  const UtdComplex e135 = std::exp(UtdComplex(0.0, 3.0 * constants::pi / 4.0));
  const UtdComplex z = std::sqrt(x) * e135;
  return std::sqrt(constants::pi * x) * e45 * detail::faddeevaW(z);
}

/// Boundary condition on a perfectly conducting wedge: Soft = Dirichlet
/// (E ⟂ / soft, reflection terms subtracted); Hard = Neumann (added).
enum class WedgeBoundary { Soft, Hard };

namespace detail {

/// a⁺(β) = 2·cos²((2πn·N⁺ − β)/2), N⁺ = nearest integer to (β+π)/(2πn).
inline double utdAplus(double beta, double n) {
  const double N = std::round((beta + constants::pi) / (constants::two_pi * n));
  const double c = std::cos((constants::two_pi * n * N - beta) / 2.0);
  return 2.0 * c * c;
}

/// a⁻(β) = 2·cos²((2πn·N⁻ − β)/2), N⁻ = nearest integer to (β−π)/(2πn).
inline double utdAminus(double beta, double n) {
  const double N = std::round((beta - constants::pi) / (constants::two_pi * n));
  const double c = std::cos((constants::two_pi * n * N - beta) / 2.0);
  return 2.0 * c * c;
}

}  // namespace detail

/// Kouyoumjian–Pathak scalar wedge diffraction coefficient for a perfectly
/// conducting wedge of exterior-angle factor n = wedgeN (n = exterior wedge
/// angle / π; n = 2 is a half-plane / knife edge). Arguments in radians:
/// `phi` observation angle, `phiPrime` incidence angle (both from the o-face
/// about the edge), `beta0` the skew angle of the incident ray to the edge.
///
///   D = −e^{-jπ/4} / (2n·√(2πk)·sin β0) ·
///        [ cot((π+(φ−φ'))/2n)·F(kL·a⁺(φ−φ'))
///        + cot((π−(φ−φ'))/2n)·F(kL·a⁻(φ−φ'))
///        ± cot((π+(φ+φ'))/2n)·F(kL·a⁺(φ+φ'))
///        ± cot((π−(φ+φ'))/2n)·F(kL·a⁻(φ+φ')) ]
///
/// The two (φ−φ') terms compensate the incident-field shadow-boundary step; the
/// two (φ+φ') terms the reflected-field step (sign − for Soft, + for Hard).
/// `distanceParamL` is the UTD distance parameter L (metres). Each cotangent ×
/// transition product stays finite as its argument sweeps through a shadow
/// boundary, so GO + diffracted is continuous. Half-plane (n = 2) recovers the
/// classic knife-edge behaviour: at the shadow boundary the total field is half
/// the incident field (−6 dB), matching knifeEdgeLossDb(0).
inline UtdComplex utdWedgeCoefficient(double phi, double phiPrime, double beta0,
                                      double wedgeN, double k,
                                      double distanceParamL = 1.0,
                                      WedgeBoundary bc = WedgeBoundary::Soft) {
  const double n = wedgeN;
  // Grazing / edge-on (sin β0 → 0) makes the 2-D coefficient blow up; clamp the
  // prefactor denominator to a small floor so the result stays finite.
  double sinB0 = std::sin(beta0);
  if (std::abs(sinB0) < 1e-9) sinB0 = std::copysign(1e-9, sinB0 == 0.0 ? 1.0 : sinB0);
  const double kL = k * distanceParamL;
  const double twoN = 2.0 * n;
  auto cot = [](double a) { return std::cos(a) / std::sin(a); };

  // cot·F is finite as its argument sweeps a shadow boundary (the cot pole and
  // the zero of F cancel), but the exact point is the indeterminate ∞·0. Since
  // the product is continuous there, nudge β by a tiny amount off any cot pole
  // (a multiple of π) — both the cot argument and the transition argument a(β)
  // are recomputed from the same nudged β, giving the correct finite limit.
  auto term = [&](double beta, bool plusInCot, bool useAplus) -> UtdComplex {
    double b = beta;
    double arg = (plusInCot ? (constants::pi + b) : (constants::pi - b)) / twoN;
    const double nearest = std::round(arg / constants::pi) * constants::pi;
    if (std::abs(arg - nearest) < 1e-7) {
      b += (plusInCot ? 1e-6 : -1e-6);
      arg = (plusInCot ? (constants::pi + b) : (constants::pi - b)) / twoN;
    }
    const double a = useAplus ? detail::utdAplus(b, n) : detail::utdAminus(b, n);
    return cot(arg) * utdTransition(kL * a);
  };

  const double betaMinus = phi - phiPrime;
  const double betaPlus = phi + phiPrime;
  const UtdComplex t1 = term(betaMinus, true, true);
  const UtdComplex t2 = term(betaMinus, false, false);
  const UtdComplex t3 = term(betaPlus, true, true);
  const UtdComplex t4 = term(betaPlus, false, false);

  const double refl = (bc == WedgeBoundary::Soft) ? -1.0 : 1.0;
  const UtdComplex bracket = (t1 + t2) + refl * (t3 + t4);

  const UtdComplex prefactor =
      -std::exp(UtdComplex(0.0, -constants::pi / 4.0)) /
      (2.0 * n * std::sqrt(constants::two_pi * k) * sinB0);
  return prefactor * bracket;
}

}  // namespace rftrace::rf
