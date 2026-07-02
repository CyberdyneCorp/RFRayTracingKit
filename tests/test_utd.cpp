// UTD wedge diffraction (D2): the Kouyoumjian-Pathak Fresnel transition function
// and the four-term conducting-wedge diffraction coefficient. References are
// ANALYTIC: F(x) -> 1 for large x; F(x) -> sqrt(pi*x)*e^{j(pi/4+x)} as x -> 0;
// the coefficient is finite; and for a half-plane (n = 2) the total field
// (GO + diffracted) is continuous across the incident shadow boundary and equals
// half the incident field there (-6 dB, the classic knife-edge grazing value).

#include <gtest/gtest.h>

#include <complex>

#include "rftrace/math.hpp"
#include "rftrace/rf/diffraction.hpp"
#include "rftrace/rf/utd.hpp"

using rftrace::rf::knifeEdgeLossDb;
using rftrace::rf::utdTransition;
using rftrace::rf::utdWedgeCoefficient;
using rftrace::rf::WedgeBoundary;
using Cd = std::complex<double>;

namespace {

constexpr double kPi = rftrace::constants::pi;

// ---------------------------------------------------------------------------
// Fresnel transition function F(x)
// ---------------------------------------------------------------------------

// F(x) -> 1 + 0j as x grows large. At x = 10 the leading asymptotic
// 1 + j/(2x) - 3/(4x^2) - ... already gives |F| ~ 0.994; by x = 50 it is within
// a few tenths of a percent.
TEST(Utd, TransitionTendsToUnityForLargeArgument) {
  EXPECT_NEAR(std::abs(utdTransition(Cd(10.0, 0.0))), 1.0, 0.02);
  EXPECT_NEAR(std::abs(utdTransition(Cd(50.0, 0.0))), 1.0, 0.01);

  const Cd f100 = utdTransition(Cd(100.0, 0.0));
  EXPECT_NEAR(std::abs(f100), 1.0, 0.005);
  EXPECT_NEAR(std::arg(f100), 0.0, 0.01);  // phase collapses to 0
}

// Large-argument reference against the published asymptotic series
//   F(x) ~ 1 + j/(2x) - 3/(4x^2) - j*15/(8x^3) + 75/(16x^4).
TEST(Utd, TransitionLargeArgumentSeriesReference) {
  const double x = 8.0;
  const Cd ref = 1.0 + Cd(0.0, 1.0) / (2.0 * x) - 3.0 / (4.0 * x * x) -
                 Cd(0.0, 1.0) * 15.0 / (8.0 * x * x * x) +
                 75.0 / (16.0 * x * x * x * x);
  const Cd f = utdTransition(Cd(x, 0.0));
  EXPECT_NEAR(f.real(), ref.real(), 2e-3);
  EXPECT_NEAR(f.imag(), ref.imag(), 2e-3);
}

// As x -> 0, F(x) -> sqrt(pi*x)*e^{j(pi/4+x)}. Checked at a small x where the
// leading form is accurate to well under a percent.
TEST(Utd, TransitionSmallArgumentReference) {
  // x small enough that the leading form is accurate to well under a percent
  // (the true F carries an O(x) correction beyond this leading term).
  const double x = 1e-10;
  const Cd ref =
      std::sqrt(kPi * x) * std::exp(Cd(0.0, kPi / 4.0 + x));
  const Cd f = utdTransition(Cd(x, 0.0));

  EXPECT_NEAR(std::abs(f), std::sqrt(kPi * x), 1e-3 * std::sqrt(kPi * x));
  EXPECT_NEAR(std::arg(f), kPi / 4.0 + x, 5e-3);
  EXPECT_NEAR(f.real(), ref.real(), 1e-3 * std::abs(ref));
  EXPECT_NEAR(f.imag(), ref.imag(), 1e-3 * std::abs(ref));
}

// Independent cross-check of the closed form at a mid-range x against a direct
// numerical evaluation of F(x) = 2j*sqrt(x)*e^{jx} * INT_{sqrt(x)}^inf e^{-j t^2} dt
// (composite Simpson over a long tail).
TEST(Utd, TransitionMatchesNumericIntegral) {
  const double x = 2.0;
  const double lo = std::sqrt(x);
  const double hi = lo + 30.0;      // Simpson panel; analytic tail beyond
  const int n = 1'500'000;          // fine grid for the oscillatory integrand
  const double h = (hi - lo) / n;
  Cd acc = 0.0;
  for (int i = 0; i <= n; ++i) {
    const double t = lo + i * h;
    const Cd val = std::exp(Cd(0.0, -t * t));
    const double w = (i == 0 || i == n) ? 1.0 : (i % 2 ? 4.0 : 2.0);
    acc += w * val;
  }
  acc *= h / 3.0;
  // INT_hi^inf e^{-j t^2} dt ~ e^{-j hi^2} / (2 j hi) (integration by parts);
  // without this the 1/t-decaying oscillatory tail leaves a ~1% error.
  const Cd tail = std::exp(Cd(0.0, -hi * hi)) / Cd(0.0, 2.0 * hi);
  const Cd fNum =
      Cd(0.0, 2.0) * std::sqrt(x) * std::exp(Cd(0.0, x)) * (acc + tail);
  const Cd f = utdTransition(Cd(x, 0.0));
  EXPECT_NEAR(f.real(), fNum.real(), 2e-3);
  EXPECT_NEAR(f.imag(), fNum.imag(), 2e-3);
}

// ---------------------------------------------------------------------------
// Wedge diffraction coefficient
// ---------------------------------------------------------------------------

// A generic geometry away from any shadow boundary yields a finite coefficient.
TEST(Utd, WedgeCoefficientIsFinite) {
  const double lambda = 0.1;                 // 3 GHz
  const double k = rftrace::constants::two_pi / lambda;
  const double n = 1.5;                       // 90-degree exterior wedge
  const Cd d = utdWedgeCoefficient(200.0 * kPi / 180.0, 60.0 * kPi / 180.0,
                                   kPi / 2.0, n, k, /*L=*/1.0);
  EXPECT_TRUE(std::isfinite(d.real()));
  EXPECT_TRUE(std::isfinite(d.imag()));
  EXPECT_GT(std::abs(d), 0.0);
}

// Total field GO + diffracted is continuous across the incident shadow boundary
// (ISB, phi = phiPrime + pi) for a half-plane (n = 2), and at the boundary it is
// half the incident field: |U_total| ~ 0.5, i.e. -6.02 dB, matching the classic
// knife-edge grazing loss knifeEdgeLossDb(0) ~ 6.03 dB.
//
// e^{-jwt} convention: incident plane wave U^i = exp(-j k rho cos(phi-phiPrime))
// (present only for phi-phiPrime < pi); outgoing diffracted cylindrical wave
// U^d = D * exp(+j k rho)/sqrt(rho); for 2D plane-wave incidence L = rho.
TEST(Utd, HalfPlaneShadowBoundaryContinuityAndHalfField) {
  const double lambda = 0.1;
  const double k = rftrace::constants::two_pi / lambda;
  const double n = 2.0;                 // half-plane / knife edge
  const double rho = 5.0;               // observation distance from the edge
  const double phiPrime = 60.0 * kPi / 180.0;
  const double sb = phiPrime + kPi;     // incident shadow boundary
  const double delta = 1e-4;            // small angular offset (rad)

  auto incident = [&](double phi) -> Cd {
    if (phi - phiPrime >= kPi) return Cd(0.0, 0.0);  // geometric shadow
    return std::exp(Cd(0.0, -k * rho * std::cos(phi - phiPrime)));
  };
  auto diffracted = [&](double phi) -> Cd {
    // Soft/Hard differ only in the reflected (phi+phiPrime) terms, which are
    // smooth across the ISB; use Soft. L = rho for 2D plane-wave incidence.
    const Cd D = utdWedgeCoefficient(phi, phiPrime, kPi / 2.0, n, k, rho,
                                     WedgeBoundary::Soft);
    return D * std::exp(Cd(0.0, k * rho)) / std::sqrt(rho);
  };
  auto total = [&](double phi) { return incident(phi) + diffracted(phi); };

  const Cd lit = total(sb - delta);
  const Cd shadow = total(sb + delta);

  // Continuity: no jump across the boundary (the GO step is cancelled). The
  // small residual is finite-delta sampling of the continuous field, ~ slope*2d.
  EXPECT_NEAR(std::abs(lit - shadow), 0.0, 5e-3);

  // Half-field at the boundary: |U_total| ~ 0.5 on both sides.
  EXPECT_NEAR(std::abs(lit), 0.5, 3e-2);
  EXPECT_NEAR(std::abs(shadow), 0.5, 3e-2);

  // -6.02 dB matches the knife-edge grazing loss to well under a dB.
  const double lossDb = -20.0 * std::log10(std::abs(lit));
  EXPECT_NEAR(lossDb, knifeEdgeLossDb(0.0), 0.5);
}

// The coefficient must stay finite EXACTLY on the shadow/reflection boundaries
// and at grazing skew (the cot·F indeterminate form and 1/sin(beta0) guards).
TEST(Utd, FiniteAtBoundariesAndGrazing) {
  const double k = 2.0 * kPi / 0.1;
  const double phiPrime = 60.0 * kPi / 180.0;
  const double n = 2.0;  // half-plane
  // Incident shadow boundary: phi = phiPrime + pi. Reflection boundary: pi - phiPrime.
  for (double phi : {phiPrime + kPi, kPi - phiPrime}) {
    const Cd d = utdWedgeCoefficient(phi, phiPrime, kPi / 2, n, k);
    EXPECT_TRUE(std::isfinite(d.real()) && std::isfinite(d.imag()));
  }
  // Grazing skew beta0 -> 0.
  const Cd g = utdWedgeCoefficient(200.0 * kPi / 180.0, phiPrime,
                                   1e-12, n, k);
  EXPECT_TRUE(std::isfinite(g.real()) && std::isfinite(g.imag()));
}

}  // namespace
