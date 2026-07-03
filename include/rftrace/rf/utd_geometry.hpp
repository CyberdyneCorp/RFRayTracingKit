#pragma once

// Wedge-geometry extraction for the geometry-driven UTD path model (Phase 1).
// Pure geometry: given an edge, the diffraction point, the tx/rx endpoints and
// the incident face(s)' opposite vertices, recover the UTD inputs the
// Kouyoumjian-Pathak coefficient documents:
//   n         wedge factor (exterior wedge angle / pi); n = 2 for a half-plane.
//   phiPrime  incidence angle about the edge, measured from the o-face.
//   phi       observation angle about the edge, measured from the o-face.
//   beta0     skew angle of the incident ray to the edge tangent (only sin used).
//   s, sPrime edge->rx and tx->edge leg lengths.
//
// The construction is deliberately normal-/winding-independent (arbitrary mesh
// triangle winding is not guaranteed outward): it uses transverse into-face
// directions plus the physical fact that both tx and rx lie in the exterior
// (vacuum) region (0, n*pi). Reciprocity is guaranteed by construction: swapping
// tx<->rx swaps phi<->phiPrime and s<->sPrime, and the wedge coefficient is
// symmetric in phi<->phiPrime while L and the spreading are symmetric in
// s<->sPrime.
//
// Scope: single wedge, convex corner (material in the smaller-alpha "V"). Concave
// / interior corners need orientation info and are out of Phase-1 scope; coplanar
// or degenerate shared edges are flagged invalid (skipped) so they diffract
// nothing.

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "rftrace/math.hpp"

namespace rftrace::rf {

/// Extracted single-wedge UTD geometry. `valid` is false for degenerate input
/// (zero-length edge, coplanar/coincident shared faces, tx/rx on/behind the
/// material, or a zero-length leg) — callers should then skip UTD for the edge.
struct WedgeGeometry {
  double n = 2.0;
  double phi = 0.0;
  double phiPrime = 0.0;
  double beta0 = constants::pi / 2.0;
  double s = 0.0;
  double sPrime = 0.0;
  bool valid = false;
};

namespace detail {

/// Wrap an angle into [0, 2*pi).
inline double wrapTwoPi(double a) {
  const double t = std::fmod(a, constants::two_pi);
  return t < 0.0 ? t + constants::two_pi : t;
}

}  // namespace detail

/// Recover the UTD wedge inputs for an edge [A,B] with diffraction point `Q`.
/// `C0` is the opposite vertex (the vertex NOT on the edge) of the o-face; `C1`
/// the opposite vertex of the second incident face for a SHARED edge (omit for a
/// FREE edge -> half-plane, n = 2).
inline WedgeGeometry extractWedgeGeometry(
    const Vec3& A, const Vec3& B, const Vec3& Q, const Vec3& tx, const Vec3& rx,
    const Vec3& C0, std::optional<Vec3> C1 = std::nullopt) {
  WedgeGeometry g;

  Vec3 e = B - A;
  const double elen = e.norm();
  if (elen <= 0.0) return g;  // degenerate edge
  e /= elen;

  const auto proj = [&](const Vec3& v) -> Vec3 { return v - v.dot(e) * e; };

  const Vec3 pu0 = proj(C0 - A);
  if (pu0.norm() <= 1e-12) return g;  // o-face vertex on the edge line
  const Vec3 u0 = pu0.normalized();

  double nPi;
  if (C1) {
    const Vec3 pu1 = proj(*C1 - A);
    if (pu1.norm() <= 1e-12) return g;
    const Vec3 u1 = pu1.normalized();
    const double d = std::clamp(u0.dot(u1), -1.0, 1.0);
    // |d| ~ 1 -> faces coplanar (flat sheet / mesh diagonal) or coincident:
    // no real dihedral, so there is no diffracting wedge.
    if (std::abs(d) > 1.0 - 1e-6) return g;
    const double alpha = std::acos(d);          // interior material "V" angle
    g.n = 2.0 - alpha / constants::pi;          // exterior = 2*pi - alpha
    nPi = g.n * constants::pi;
  } else {
    g.n = 2.0;
    nPi = constants::two_pi;
  }

  // Transverse McNamara frame from the o-face. w = e x u0 completes (u0, w);
  // angles are measured CCW from u0 about the edge, wrapped into [0, 2*pi).
  const auto anglesFor = [&](const Vec3& edge) {
    const Vec3 w = edge.cross(u0);
    const auto ang = [&](const Vec3& dir) {
      const Vec3 pd = dir - dir.dot(edge) * edge;
      return detail::wrapTwoPi(std::atan2(pd.dot(w), pd.dot(u0)));
    };
    return std::pair<double, double>{ang(tx - Q), ang(rx - Q)};
  };
  const auto inExterior = [&](double a) {
    return a > 1e-9 && a < nPi - 1e-9;
  };

  auto [pp, ph] = anglesFor(e);
  if (C1) {
    // The exterior (vacuum) region is (0, n*pi) and BOTH tx and rx lie in it.
    // Exactly one edge orientation places both angles there; flipping e negates
    // the angles (a -> 2*pi - a). If neither orientation qualifies, tx/rx are
    // on/behind the material -> guard.
    if (!(inExterior(pp) && inExterior(ph))) {
      auto [pp2, ph2] = anglesFor(-e);
      if (!(inExterior(pp2) && inExterior(ph2))) return g;
      pp = pp2;
      ph = ph2;
    }
  }
  g.phiPrime = pp;
  g.phi = ph;

  // beta0: skew of the incident ray to the edge. At the Fermat point Q both legs
  // share the same sin(beta0) (Keller cone), and only sin(beta0) is used, so the
  // choice of leg (and edge sign) does not affect the loss.
  const Vec3 inc = Q - tx;
  const double incn = inc.norm();
  const double cb =
      incn > 0.0 ? std::clamp((inc / incn).dot(e), -1.0, 1.0) : 0.0;
  g.beta0 = std::acos(cb);

  g.s = (rx - Q).norm();
  g.sPrime = (tx - Q).norm();
  g.valid = g.s > 0.0 && g.sPrime > 0.0;
  return g;
}

}  // namespace rftrace::rf
