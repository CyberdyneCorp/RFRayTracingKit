#include "rftrace/simulator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "rftrace/cell_planning.hpp"
#include "rftrace/detail/batch_query.hpp"
#include "rftrace/detail/parallel_for.hpp"
#include "rftrace/detail/propagation.hpp"
#include "rftrace/rf/channel.hpp"
#include "rftrace/rf/diffraction.hpp"
#include "rftrace/rf/diffraction_multi.hpp"
#include "rftrace/rf/doppler.hpp"
#include "rftrace/rf/reflection.hpp"
#include "rftrace/rf/utd.hpp"

namespace rftrace {
namespace detail {

std::optional<RFPath> losPath(const IBackend& backend, const Transmitter& tx,
                              const Receiver& rx,
                              const PropagationContext* ctx) {
  const Ray los = segmentRay(tx.position, rx.position, kEps);
  if (los.tMax <= los.tMin) return std::nullopt;
  const bool blocked = (ctx && ctx->settings.enableVegetation)
                           ? blockedByNonVegetation(ctx->scene, backend, los)
                           : backend.occluded(los);
  if (blocked) return std::nullopt;

  return buildLosPath(tx, rx, ctx);
}

namespace {

/// Build one specular reflection path through the ordered triangle sequence
/// `seq` using the image method. Returns false on invalid geometry/occlusion.
bool buildReflection(const Scene& scene, const IBackend& backend,
                     const Transmitter& tx, const Receiver& rx,
                     const std::vector<int>& seq, RFPath& out,
                     const PropagationContext* ctx) {
  const auto& tris = scene.triangles();

  std::vector<Vec3> images;
  images.reserve(seq.size() + 1);
  images.push_back(tx.position);
  for (int idx : seq) {
    const Vec3 n = tris[idx].normal();
    if (n.norm() == 0.0) return false;
    images.push_back(mirrorPoint(images.back(), tris[idx].v0, n));
  }

  const int k = static_cast<int>(seq.size());
  std::vector<Vec3> reflPts(k);
  Vec3 target = rx.position;
  for (int i = k - 1; i >= 0; --i) {
    const Triangle& tri = tris[seq[i]];
    Vec3 p;
    if (!segmentPlane(images[i + 1], target, tri.v0, tri.normal(), p))
      return false;
    if (!pointInTriangle(p, tri)) return false;
    reflPts[i] = p;
    target = p;
  }

  out.points.clear();
  out.points.push_back(tx.position);
  for (const Vec3& p : reflPts) out.points.push_back(p);
  out.points.push_back(rx.position);

  for (std::size_t i = 1; i < out.points.size(); ++i) {
    const Ray seg = segmentRay(out.points[i - 1], out.points[i], kEps);
    if (seg.tMax <= seg.tMin) return false;
    const bool blocked = (ctx && ctx->settings.enableVegetation)
                             ? blockedByNonVegetation(scene, backend, seg)
                             : backend.occluded(seg);
    if (blocked) return false;
  }

  double reflLoss = 0.0;
  // Track the polarization state through the bounces (depolarization): each
  // reflection applies its complex TE/TM Fresnel coefficients to the Jones
  // vector, so unequal TE/TM (or phase) rotates linear pol / makes it elliptical.
  // Amplitude loss stays in reflLoss; the mismatch normalizes magnitude, so this
  // affects only the polarization-mismatch term, not double-counted attenuation.
  rf::Jones jones = rf::jonesFor(tx.polarization);
  out.materialHits.clear();
  for (int i = 0; i < k; ++i) {
    const Triangle& tri = tris[seq[i]];
    const Material& mat = scene.materialForTriangle(seq[i]);
    const Vec3 inDir = (out.points[i + 1] - out.points[i]).normalized();
    const double cosI = std::clamp(std::abs(inDir.dot(tri.normal())), 0.0, 1.0);
    const double incidence = std::acos(cosI);
    reflLoss += rf::reflectionLossDb(mat, incidence, tx.polarization,
                                     tx.frequencyHz);
    if (ctx && ctx->settings.enableDepolarization && mat.hasElectricalParameters()) {
      const rf::Complex epsc = rf::complexPermittivity(
          mat.relativePermittivity, mat.conductivity, tx.frequencyHz);
      const rf::Complex te = rf::fresnelReflectionCoefficient(
          epsc, incidence, rf::FresnelPolarization::TE);
      const rf::Complex tm = rf::fresnelReflectionCoefficient(
          epsc, incidence, rf::FresnelPolarization::TM);
      jones = rf::reflectDepolarize(jones, te, tm);
    }
    out.materialHits.push_back(mat.name);
  }

  out.transmitterId = tx.id;
  out.receiverId = rx.id;
  out.type = PathType::Reflection;
  out.reflections = k;
  finishPath(out, tx, rx, reflLoss, ctx, &jones);
  return true;
}

void enumerateReflections(const Scene& scene, const IBackend& backend,
                          const Transmitter& tx, const Receiver& rx, int depth,
                          std::vector<int>& seq, std::vector<RFPath>& out,
                          const PropagationContext* ctx) {
  if (static_cast<int>(seq.size()) == depth) {
    RFPath path;
    if (buildReflection(scene, backend, tx, rx, seq, path, ctx))
      out.push_back(std::move(path));
    return;
  }
  const int n = static_cast<int>(scene.triangles().size());
  for (int t = 0; t < n; ++t) {
    if (!seq.empty() && seq.back() == t) continue;
    seq.push_back(t);
    enumerateReflections(scene, backend, tx, rx, depth, seq, out, ctx);
    seq.pop_back();
  }
}

}  // namespace

std::vector<RFPath> imageMethodReflections(const Scene& scene,
                                           const IBackend& backend,
                                           const Transmitter& tx,
                                           const Receiver& rx,
                                           int maxReflections,
                                           const PropagationContext* ctx) {
  std::vector<RFPath> paths;
  std::vector<int> seq;
  for (int depth = 1; depth <= maxReflections; ++depth)
    enumerateReflections(scene, backend, tx, rx, depth, seq, paths, ctx);
  return paths;
}

namespace {

// Point on segment [a,b] that minimizes the total detour |tx→P| + |P→rx|
// (Fermat's stationary point for a straight edge). The objective is convex in
// the segment parameter, so a ternary search converges to the diffraction point.
Vec3 diffractionPointOnEdge(const Vec3& tx, const Vec3& rx, const Vec3& a,
                            const Vec3& b) {
  const auto detour = [&](double s) {
    const Vec3 p = a + s * (b - a);
    return (tx - p).norm() + (p - rx).norm();
  };
  double lo = 0.0, hi = 1.0;
  for (int i = 0; i < 100; ++i) {
    const double m1 = lo + (hi - lo) / 3.0;
    const double m2 = hi - (hi - lo) / 3.0;
    if (detour(m1) < detour(m2))
      hi = m2;
    else
      lo = m1;
  }
  return a + 0.5 * (lo + hi) * (b - a);
}

using EdgeKey = std::array<double, 6>;

EdgeKey makeEdgeKey(const Vec3& a, const Vec3& b) {
  const EdgeKey ka{a.x(), a.y(), a.z(), b.x(), b.y(), b.z()};
  const EdgeKey kb{b.x(), b.y(), b.z(), a.x(), a.y(), a.z()};
  return ka < kb ? ka : kb;  // order-independent canonical key
}

// Open silhouette edges: those belonging to exactly one triangle. Interior/shared
// edges (mesh diagonals of a quad, closed ridges) are excluded — treating them as
// knife edges would diffract over triangulation artifacts. This matches the R2
// scope (single dominant edge; multi-edge / ridge diffraction deferred).
std::vector<std::pair<Vec3, Vec3>> boundaryEdges(
    const std::vector<Triangle>& tris) {
  static constexpr int kEdge[3][2] = {{0, 1}, {1, 2}, {2, 0}};
  std::map<EdgeKey, int> counts;
  for (const Triangle& t : tris) {
    const Vec3 v[3] = {t.v0, t.v1, t.v2};
    for (const auto& e : kEdge) ++counts[makeEdgeKey(v[e[0]], v[e[1]])];
  }
  std::vector<std::pair<Vec3, Vec3>> edges;
  for (const auto& [k, c] : counts)
    if (c == 1)
      edges.emplace_back(Vec3{k[0], k[1], k[2]}, Vec3{k[3], k[4], k[5]});
  return edges;
}

struct EdgeCandidate {
  double v = 0.0;
  Vec3 point{0, 0, 0};
};

// Fresnel parameter v (> 0) and diffraction point for a detour over edge [a,b],
// when the edge obstructs LOS and both detour segments clear the scene; nullopt
// otherwise.
std::optional<EdgeCandidate> evaluateEdge(const IBackend& backend,
                                          const Vec3& tx, const Vec3& rx,
                                          const Vec3& a, const Vec3& b,
                                          double wavelength) {
  const Vec3 point = diffractionPointOnEdge(tx, rx, a, b);
  const auto g = rf::diffractionGeometry(tx, rx, point);
  if (g.d1 <= kEps || g.d2 <= kEps || g.clearanceMeters <= 0.0)
    return std::nullopt;
  const double v =
      rf::fresnelDiffractionParameter(g.clearanceMeters, g.d1, g.d2, wavelength);
  if (v <= 0.0) return std::nullopt;
  const Ray s1 = segmentRay(tx, point, kEps);
  const Ray s2 = segmentRay(point, rx, kEps);
  if (s1.tMax <= s1.tMin || s2.tMax <= s2.tMin) return std::nullopt;
  if (backend.occluded(s1) || backend.occluded(s2)) return std::nullopt;
  return EdgeCandidate{v, point};
}

// Number of interior samples taken along the tx→rx horizontal baseline when
// building a terrain+building profile for multi-edge diffraction.
constexpr int kDiffractionProfileSamples = 128;

// Build a vertical-plane profile between tx and rx by sampling the maximum
// surface height at each interior horizontal position: a ray is cast straight
// down from far above and its first hit gives the obstacle top there. Samples
// that hit no geometry contribute no obstacle. Distances are horizontal (m);
// heights use the world z datum (same as tx/rx z).
rf::TerrainProfile buildTerrainProfile(const IBackend& backend, const Vec3& tx,
                                       const Vec3& rx) {
  rf::TerrainProfile prof;
  prof.txHeightMeters = tx.z();
  prof.rxHeightMeters = rx.z();
  const Vec3 txXY{tx.x(), tx.y(), 0.0};
  const Vec3 rxXY{rx.x(), rx.y(), 0.0};
  const double D = (rxXY - txXY).norm();
  prof.totalDistanceMeters = D;
  if (D <= kEps) return prof;

  const Vec3 dirXY = (rxXY - txXY) / D;
  constexpr double kZTop = 1e6;  // ray origin well above any plausible surface
  for (int i = 1; i <= kDiffractionProfileSamples; ++i) {
    const double f = static_cast<double>(i) / (kDiffractionProfileSamples + 1);
    const Vec3 xy = txXY + (f * D) * dirXY;
    const Ray down(Vec3{xy.x(), xy.y(), kZTop}, Vec3{0.0, 0.0, -1.0}, kEps);
    const Hit h = backend.closestHit(down);
    if (!h.valid) continue;
    prof.obstacles.push_back({f * D, kZTop - h.t});
  }
  return prof;
}

// Diffraction loss (dB) for a blocked link with a known dominant edge `v`.
// Default (SingleEdge / no context) is the ITU-R P.526 knife-edge loss; the
// multi-edge models build a terrain profile and apply Bullington / Deygout,
// falling back to the single edge when the profile has no obstacles.
double diffractionLossDb(const IBackend& backend, const Vec3& tx, const Vec3& rx,
                         double v, double wavelength,
                         const PropagationContext* ctx) {
  const double single = rf::knifeEdgeLossDb(v);
  if (!ctx || ctx->settings.diffractionModel == DiffractionModel::SingleEdge)
    return single;
  // UTD: single dominant edge as a conducting half-plane (uses the same v).
  if (ctx->settings.diffractionModel == DiffractionModel::UTD)
    return rf::utdDiffractionLossDb(v);
  const rf::TerrainProfile prof = buildTerrainProfile(backend, tx, rx);
  if (prof.obstacles.empty()) return single;
  return ctx->settings.diffractionModel == DiffractionModel::Bullington
             ? rf::bullingtonLossDb(prof, wavelength)
             : rf::deygoutLossDb(prof, wavelength);
}

}  // namespace

std::optional<RFPath> diffractionPath(const Scene& scene,
                                      const IBackend& backend,
                                      const Transmitter& tx, const Receiver& rx,
                                      double frequencyHz,
                                      const PropagationContext* ctx) {
  if (frequencyHz <= 0.0) return std::nullopt;
  if ((rx.position - tx.position).norm() <= kEps) return std::nullopt;
  const double wavelength = constants::c / frequencyHz;

  // Dominant single edge = the strongest diffracted detour, i.e. the smallest
  // Fresnel parameter (least knife-edge loss) among valid silhouette edges.
  bool found = false;
  EdgeCandidate best;
  for (const auto& [a, b] : boundaryEdges(scene.triangles())) {
    if (auto c = evaluateEdge(backend, tx.position, rx.position, a, b,
                              wavelength)) {
      if (!found || c->v < best.v) {
        found = true;
        best = *c;
      }
    }
  }
  if (!found) return std::nullopt;

  RFPath p;
  p.transmitterId = tx.id;
  p.receiverId = rx.id;
  p.type = PathType::Diffraction;
  p.diffractions = 1;
  p.points = {tx.position, best.point, rx.position};
  const double loss = diffractionLossDb(backend, tx.position, rx.position,
                                        best.v, wavelength, ctx);
  finishPath(p, tx, rx, loss, ctx);
  return p;
}

void aggregate(ReceiverResult& rr, bool coherent) {
  if (rr.paths.empty()) return;

  std::vector<double> powers, phases, delays;
  powers.reserve(rr.paths.size());
  for (const RFPath& p : rr.paths) {
    powers.push_back(p.receivedPowerDbm);
    phases.push_back(p.phaseRad);
    delays.push_back(p.delaySeconds);
  }
  rr.hasSignal = true;
  rr.receivedPowerDbm = coherent ? rf::aggregateCoherentDbm(powers, phases)
                                 : rf::aggregateIncoherentDbm(powers);
  rr.delaySpreadNs = rf::rmsDelaySpreadSeconds(powers, delays) * 1e9;

  const auto strongest = std::max_element(
      rr.paths.begin(), rr.paths.end(),
      [](const RFPath& a, const RFPath& b) {
        return a.receivedPowerDbm < b.receivedPowerDbm;
      });
  rr.pathLossDb = strongest->pathLossDb;
  rr.phaseRad = strongest->phaseRad;
}

namespace {

// Evaluate a single point receiver against every transmitter, mirroring the
// per-receiver path collection and aggregation used by Simulator::run (LOS /
// diffraction, then reflections via the active mode). Used by runRoute.
ReceiverResult evaluatePointReceiver(const Scene& scene, IBackend& backend,
                                     const Receiver& rx,
                                     const SimulationSettings& settings,
                                     const PropagationContext& ctx) {
  ReceiverResult rr;
  rr.receiverId = rx.id;
  rr.position = rx.position;

  for (const Transmitter& tx : scene.transmitters()) {
    if (auto los = losPath(backend, tx, rx, &ctx)) {
      rr.paths.push_back(std::move(*los));
    } else if (settings.enableDiffraction) {
      if (auto dif =
              diffractionPath(scene, backend, tx, rx, tx.frequencyHz, &ctx))
        rr.paths.push_back(std::move(*dif));
    }
  }

  if (settings.mode == PropagationMode::ImageMethod) {
    for (const Transmitter& tx : scene.transmitters()) {
      auto refl = imageMethodReflections(scene, backend, tx, rx,
                                         settings.maxReflections, &ctx);
      for (auto& p : refl) rr.paths.push_back(std::move(p));
    }
  } else {  // RayLaunch
    const std::vector<Receiver> one{rx};
    for (const Transmitter& tx : scene.transmitters()) {
      auto perRx = rayLaunch(scene, backend, tx, one, settings, &ctx);
      for (auto& p : perRx[0]) rr.paths.push_back(std::move(p));
    }
  }

  aggregate(rr, settings.coherent);
  return rr;
}

}  // namespace

}  // namespace detail

namespace {

/// FNV-1a/64 content hash over a triangle soup: folds the triangle COUNT then
/// every coordinate of every triangle by its IEEE-754 bits (component-wise so it
/// is padding-agnostic). Pure function of the geometry payload — never of the
/// vector's address, capacity, or identity — so any coordinate or count change
/// flips the key while an unchanged scene keeps it. O(triangles), far cheaper
/// than building a BVH/OptiX GAS, so it is a net win for repeated runs.
std::uint64_t geometryKey(const std::vector<Triangle>& tris) {
  constexpr std::uint64_t kOffset = 1469598103934665603ull;
  constexpr std::uint64_t kPrime = 1099511628211ull;
  auto foldU64 = [](std::uint64_t h, std::uint64_t v) {
    for (int b = 0; b < 8; ++b) {
      h ^= (v >> (b * 8)) & 0xffu;
      h *= kPrime;
    }
    return h;
  };
  auto foldDouble = [&](std::uint64_t h, double d) {
    std::uint64_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    return foldU64(h, bits);
  };
  std::uint64_t h = kOffset;
  h = foldU64(h, static_cast<std::uint64_t>(tris.size()));  // count in the key
  for (const Triangle& t : tris)
    for (const Vec3* v : {&t.v0, &t.v1, &t.v2}) {
      h = foldDouble(h, v->x());
      h = foldDouble(h, v->y());
      h = foldDouble(h, v->z());
    }
  return h;
}

}  // namespace

IBackend& Simulator::ensureBackend(const Scene& scene) const {
  const std::vector<Triangle>& tris = scene.triangles();
  const std::uint64_t key = geometryKey(tris);
  // Validity requires a cached backend AND a key match, so a legitimate key of 0
  // never causes a false reuse.
  if (cachedBackend_ && cachedKey_ == key) return *cachedBackend_;
  std::unique_ptr<IBackend> backend =
      makeBackend(settings_.backend, settings_.allowBackendFallback);
  backend->build(tris);
  cachedBackend_ = std::move(backend);
  cachedKey_ = key;
  ++backendRebuildCount_;
  return *cachedBackend_;
}

RFResult Simulator::run(const Scene& scene) const {
  IBackend* backend = &ensureBackend(scene);

  RFResult result;
  result.simulationId = settings_.simulationId;
  result.frequencyHz = scene.transmitters().empty()
                           ? 0.0
                           : scene.transmitters().front().frequencyHz;
  for (const Transmitter& tx : scene.transmitters())
    result.transmitters.push_back(
        {tx.id, tx.position, tx.frequencyHz, tx.powerDbm});

  const detail::PropagationContext ctx{settings_, scene, *backend,
                                       result.frequencyHz};

  const auto& receivers = scene.receivers();
  std::vector<ReceiverResult> rrs(receivers.size());
  for (std::size_t i = 0; i < receivers.size(); ++i) {
    rrs[i].receiverId = receivers[i].id;
    rrs[i].position = receivers[i].position;
  }

  // LOS is deterministic in both modes. When LOS is blocked and diffraction is
  // enabled, attempt a single dominant knife-edge detour instead. With
  // vegetation OFF the plain occlusion decisions are batched into a single
  // backend dispatch; the vegetation walk (D5) stays per-ray. The follow-up
  // (LOS push, else diffraction) runs in the SAME nested order in both cases,
  // so per-receiver path insertion order is preserved.
  if (settings_.enableVegetation) {
    for (std::size_t i = 0; i < receivers.size(); ++i)
      for (const Transmitter& tx : scene.transmitters()) {
        if (auto los = detail::losPath(*backend, tx, receivers[i], &ctx)) {
          rrs[i].paths.push_back(std::move(*los));
        } else if (settings_.enableDiffraction) {
          if (auto dif = detail::diffractionPath(scene, *backend, tx,
                                                 receivers[i], tx.frequencyHz,
                                                 &ctx))
            rrs[i].paths.push_back(std::move(*dif));
        }
      }
  } else {
    detail::BatchQuery q;
    std::vector<std::size_t> tokens;
    tokens.reserve(receivers.size() * scene.transmitters().size());
    for (std::size_t i = 0; i < receivers.size(); ++i)
      for (const Transmitter& tx : scene.transmitters()) {
        const Ray los = segmentRay(tx.position, receivers[i].position,
                                   detail::kEps);
        tokens.push_back(los.tMax <= los.tMin ? detail::BatchQuery::kNoRay
                                              : q.add(los));
      }
    q.runOcclusion(*backend);
    std::size_t k = 0;
    for (std::size_t i = 0; i < receivers.size(); ++i)
      for (const Transmitter& tx : scene.transmitters()) {
        const std::size_t tok = tokens[k++];
        const bool blocked =
            tok == detail::BatchQuery::kNoRay || q.occluded(tok);
        if (!blocked) {
          rrs[i].paths.push_back(
              detail::buildLosPath(tx, receivers[i], &ctx));
        } else if (settings_.enableDiffraction) {
          if (auto dif = detail::diffractionPath(scene, *backend, tx,
                                                 receivers[i], tx.frequencyHz,
                                                 &ctx))
            rrs[i].paths.push_back(std::move(*dif));
        }
      }
  }

  if (settings_.mode == PropagationMode::ImageMethod) {
    // Per-receiver reflection collection is independent: each i appends only to
    // rrs[i].paths (LOS was already pushed above), and imageMethodReflections is
    // a pure function of (scene, backend, tx, rx) doing const backend queries.
    // Parallelize over disjoint slots only on the thread-safe CPU backend; any
    // non-reentrant backend runs the exact serial path (tc == 1).
    const int tc =
        backend->kind() == Backend::CPU ? settings_.threadCount : 1;
    detail::parallelFor(receivers.size(), tc, [&](std::size_t i) {
      for (const Transmitter& tx : scene.transmitters()) {
        auto refl = detail::imageMethodReflections(
            scene, *backend, tx, receivers[i], settings_.maxReflections, &ctx);
        for (auto& p : refl) rrs[i].paths.push_back(std::move(p));
      }
    });
  } else {  // RayLaunch
    for (const Transmitter& tx : scene.transmitters()) {
      auto perRx =
          detail::rayLaunch(scene, *backend, tx, receivers, settings_, &ctx);
      for (std::size_t i = 0; i < receivers.size(); ++i)
        for (auto& p : perRx[i]) rrs[i].paths.push_back(std::move(p));
    }
  }

  for (auto& rr : rrs) {
    detail::aggregate(rr, settings_.coherent);
    result.receivers.push_back(std::move(rr));
  }

  // Serving-cell + SINR is computed after per-receiver aggregation across ALL
  // transmitters, and only when enabled (leaves default results untouched).
  if (settings_.enableSinr)
    for (auto& rr : result.receivers) applySinr(rr, settings_);

  return result;
}

namespace {

// Write an aggregated cell result into the coverage arrays at row-major `idx`.
// Unreached cells (no signal) keep the NoSignal sentinel already stored there.
void storeCell(CoverageResult& cov, int idx, ReceiverResult& rr,
               const SimulationSettings& settings) {
  detail::aggregate(rr, settings.coherent);
  if (!rr.hasSignal) return;
  cov.powerDbm[idx] = rr.receivedPowerDbm;
  cov.pathLossDb[idx] = rr.pathLossDb;
  if (settings.enableSinr) cov.sinrDb[idx] = computeSinr(rr, settings).sinrDb;
}

// Ray-launch multipath coverage (D5). Launches rays ONCE per transmitter with
// each grid cell treated as a capture point (radius ≈ cellSize/2, at the cell's
// terrain/eval height) and accumulates the captured LOS + reflected power per
// cell. Cells are indexed row-major to match the coverage arrays. Fully
// shadowed cells (no LOS, no captured reflection) fall back to the per-cell
// knife-edge / multi-edge diffraction detour when enableDiffraction is set.
void fillCoverageMultipath(const Scene& scene, IBackend& backend,
                           const CoverageGrid& grid,
                           const SimulationSettings& settings,
                           const detail::PropagationContext& ctx,
                           CoverageResult& cov) {
  std::vector<Receiver> cells;
  cells.reserve(grid.cellCount());
  for (int row = 0; row < grid.rows; ++row)
    for (int col = 0; col < grid.cols; ++col) {
      Receiver rx;
      rx.id = "cell";
      rx.position = grid.cellCenter(row, col);
      cells.push_back(rx);
    }

  std::vector<ReceiverResult> rrs(cells.size());
  for (std::size_t i = 0; i < cells.size(); ++i) {
    rrs[i].receiverId = cells[i].id;
    rrs[i].position = cells[i].position;
  }

  // LOS is deterministic and added per cell (rayLaunch returns only
  // reflections). With vegetation OFF the plain occlusion decisions are batched
  // into a single dispatch; the vegetation walk (D5) stays per-ray. Insertion
  // order over `for i(cells): for tx:` is preserved in both cases.
  if (settings.enableVegetation) {
    for (std::size_t i = 0; i < cells.size(); ++i)
      for (const Transmitter& tx : scene.transmitters())
        if (auto los = detail::losPath(backend, tx, cells[i], &ctx))
          rrs[i].paths.push_back(std::move(*los));
  } else {
    detail::BatchQuery q;
    std::vector<std::size_t> tokens;
    tokens.reserve(cells.size() * scene.transmitters().size());
    for (std::size_t i = 0; i < cells.size(); ++i)
      for (const Transmitter& tx : scene.transmitters()) {
        const Ray los =
            segmentRay(tx.position, cells[i].position, detail::kEps);
        tokens.push_back(los.tMax <= los.tMin ? detail::BatchQuery::kNoRay
                                              : q.add(los));
      }
    q.runOcclusion(backend);
    std::size_t k = 0;
    for (std::size_t i = 0; i < cells.size(); ++i)
      for (const Transmitter& tx : scene.transmitters()) {
        const std::size_t tok = tokens[k++];
        const bool blocked =
            tok == detail::BatchQuery::kNoRay || q.occluded(tok);
        if (!blocked)
          rrs[i].paths.push_back(detail::buildLosPath(tx, cells[i], &ctx));
      }
  }

  // Reflected multipath: one ray launch per transmitter over all capture cells.
  for (const Transmitter& tx : scene.transmitters()) {
    auto perCell = detail::rayLaunch(scene, backend, tx, cells, settings, &ctx);
    for (std::size_t i = 0; i < cells.size(); ++i)
      for (auto& p : perCell[i]) rrs[i].paths.push_back(std::move(p));
  }

  // Diffraction fill for fully shadowed cells (no LOS and no captured reflection).
  if (settings.enableDiffraction)
    for (std::size_t i = 0; i < cells.size(); ++i)
      if (rrs[i].paths.empty())
        for (const Transmitter& tx : scene.transmitters())
          if (auto dif = detail::diffractionPath(scene, backend, tx, cells[i],
                                                 tx.frequencyHz, &ctx))
            rrs[i].paths.push_back(std::move(*dif));

  for (std::size_t i = 0; i < cells.size(); ++i)
    storeCell(cov, static_cast<int>(i), rrs[i], settings);
}

// Deterministic per-cell image-method coverage (the default): each cell centre
// is an exact point receiver evaluated against LOS + specular reflections.
void fillCoverageImageMethod(const Scene& scene, IBackend& backend,
                             const CoverageGrid& grid,
                             const SimulationSettings& settings,
                             const detail::PropagationContext& ctx,
                             CoverageResult& cov) {
  const auto& txs = scene.transmitters();
  const bool batchLos = !settings.enableVegetation;

  // Pre-pass (vegetation OFF): batch every cell x tx LOS occlusion decision in
  // row-major/tx order into a single backend dispatch. Vegetation-on keeps the
  // per-ray losPath walk (D5). Reflections stay per-ray and are computed inline
  // in the main loop, so the per-cell interleave LOS(tx),refl(tx) — and thus
  // path insertion order — is unchanged.
  detail::BatchQuery q;
  std::vector<std::size_t> tokens;
  if (batchLos) {
    tokens.reserve(static_cast<std::size_t>(grid.cellCount()) * txs.size());
    for (int row = 0; row < grid.rows; ++row)
      for (int col = 0; col < grid.cols; ++col) {
        const Vec3 center = grid.cellCenter(row, col);
        for (const Transmitter& tx : txs) {
          const Ray los = segmentRay(tx.position, center, detail::kEps);
          tokens.push_back(los.tMax <= los.tMin ? detail::BatchQuery::kNoRay
                                                : q.add(los));
        }
      }
    q.runOcclusion(backend);
  }

  // Per-cell evaluation is independent: cell i builds its own ReceiverResult and
  // storeCell writes only the disjoint cov slots at index i. The batched-LOS
  // pre-pass above (q.runOcclusion) is serial; q.occluded() is a const lookup,
  // safe for concurrent reads. The running token cursor is replaced by the equal
  // index-derived base offset i*txs.size() (tokens were filled in exactly
  // row-major-cell / tx-minor order), making each cell independent of any other.
  // Parallelize over disjoint slots only on the thread-safe CPU backend; any
  // non-reentrant backend runs the exact serial path (tc == 1).
  const std::size_t cellCount = static_cast<std::size_t>(grid.cellCount());
  const std::size_t txCount = txs.size();
  const int tc = backend.kind() == Backend::CPU ? settings.threadCount : 1;
  detail::parallelFor(cellCount, tc, [&](std::size_t i) {
    const int row = static_cast<int>(i) / grid.cols;
    const int col = static_cast<int>(i) % grid.cols;
    Receiver rx;
    rx.id = "cell";
    rx.position = grid.cellCenter(row, col);

    ReceiverResult rr;
    rr.receiverId = rx.id;
    rr.position = rx.position;
    const std::size_t tokenBase = i * txCount;
    std::size_t j = 0;
    for (const Transmitter& tx : txs) {
      if (batchLos) {
        const std::size_t tok = tokens[tokenBase + j];
        const bool blocked =
            tok == detail::BatchQuery::kNoRay || q.occluded(tok);
        if (!blocked) rr.paths.push_back(detail::buildLosPath(tx, rx, &ctx));
      } else if (auto los = detail::losPath(backend, tx, rx, &ctx)) {
        rr.paths.push_back(std::move(*los));
      }
      auto refl = detail::imageMethodReflections(
          scene, backend, tx, rx, settings.maxReflections, &ctx);
      for (auto& p : refl) rr.paths.push_back(std::move(p));
      ++j;
    }
    storeCell(cov, static_cast<int>(i), rr, settings);
  });
}

}  // namespace

CoverageResult Simulator::runCoverage(const Scene& scene,
                                      const CoverageGrid& grid) const {
  IBackend* backend = &ensureBackend(scene);

  CoverageResult cov;
  cov.grid = grid;
  cov.simulationId = settings_.simulationId;
  cov.frequencyHz = scene.transmitters().empty()
                        ? 0.0
                        : scene.transmitters().front().frequencyHz;
  cov.powerDbm.assign(grid.cellCount(), CoverageResult::NoSignal);
  cov.pathLossDb.assign(grid.cellCount(), CoverageResult::NoSignal);
  if (settings_.enableSinr)
    cov.sinrDb.assign(grid.cellCount(), CoverageResult::NoSignal);

  // Ray-launch coverage (D5) accumulates specular multipath per cell; the
  // capture radius is derived from the grid so each cell is its own capture
  // point. The default image-method path is exact and left bit-for-bit
  // unchanged. A local settings copy carries the derived capture radius without
  // mutating the simulator's configuration.
  if (settings_.mode == PropagationMode::RayLaunch) {
    SimulationSettings mp = settings_;
    mp.captureRadius = 0.5 * grid.cellSize;
    const detail::PropagationContext ctx{mp, scene, *backend, cov.frequencyHz};
    fillCoverageMultipath(scene, *backend, grid, mp, ctx, cov);
  } else {
    const detail::PropagationContext ctx{settings_, scene, *backend,
                                         cov.frequencyHz};
    fillCoverageImageMethod(scene, *backend, grid, settings_, ctx, cov);
  }
  return cov;
}

namespace {

/// Receiver velocity (m/s) at route sample `i`, derived from the sampled
/// positions (D4). Uses a central difference over the neighboring samples
/// (forward/backward at the endpoints) for the direction. With a configured
/// `speedMps > 0` the magnitude is fixed to that speed; otherwise the velocity
/// is the per-step displacement itself (unit sample time), so its magnitude
/// tracks the sample spacing. A single-sample route or coincident neighbors
/// yield the zero vector.
Vec3 sampleVelocity(const std::vector<RouteSamplePoint>& pts, std::size_t i,
                    double speedMps) {
  const std::size_t n = pts.size();
  if (n <= 1) return Vec3::Zero();
  const std::size_t lo = i > 0 ? i - 1 : i;
  const std::size_t hi = i + 1 < n ? i + 1 : i;
  const Vec3 disp = pts[hi].position - pts[lo].position;
  const double dispNorm = disp.norm();
  if (dispNorm <= 0.0) return Vec3::Zero();
  if (speedMps > 0.0) return Vec3(speedMps * (disp / dispNorm));
  const double steps = static_cast<double>(hi - lo);
  return Vec3(disp / steps);
}

/// Fill each path's Doppler shift from the receiver velocity and the path's own
/// arrival direction, returning the aggregate max |f_d| (Hz) over the paths.
double fillPathDoppler(std::vector<RFPath>& paths, const Vec3& velocity,
                       double frequencyHz) {
  double maxAbs = 0.0;
  for (RFPath& p : paths) {
    if (p.points.size() < 2) continue;
    // Arrival direction: from the receiver (last point) toward the last hop.
    const Vec3 arrival = p.points[p.points.size() - 2] - p.points.back();
    p.dopplerHz = rf::perPathDopplerHz(velocity, arrival, frequencyHz);
    maxAbs = std::max(maxAbs, std::abs(p.dopplerHz));
  }
  return maxAbs;
}

}  // namespace

RouteResult Simulator::runRoute(const Scene& scene, const Route& route) const {
  IBackend* backend = &ensureBackend(scene);

  RouteResult out;
  out.routeId = route.id;
  out.simulationId = settings_.simulationId;
  out.frequencyHz = scene.transmitters().empty()
                        ? 0.0
                        : scene.transmitters().front().frequencyHz;

  const detail::PropagationContext ctx{settings_, scene, *backend,
                                       out.frequencyHz};

  const std::vector<RouteSamplePoint> points = route.sample();
  out.samples.reserve(points.size());
  for (std::size_t i = 0; i < points.size(); ++i) {
    Receiver rx;
    rx.id = route.id + "/" + std::to_string(i);
    rx.position = points[i].position;
    rx.antenna = route.antenna;
    rx.polarization = route.polarization;

    ReceiverResult rr =
        detail::evaluatePointReceiver(scene, *backend, rx, settings_, ctx);
    if (settings_.enableSinr) applySinr(rr, settings_);

    // Per-path Doppler (D4): derive this sample's receiver velocity from the
    // sampled route geometry and fill each path's shift. A static / single
    // sample yields zero velocity, so every dopplerHz stays 0.
    const Vec3 velocity = sampleVelocity(points, i, route.speedMps);
    const double sampleDoppler =
        fillPathDoppler(rr.paths, velocity, out.frequencyHz);

    RouteSample s;
    s.index = static_cast<int>(i);
    s.distanceMeters = points[i].distanceMeters;
    s.position = points[i].position;
    s.hasSignal = rr.hasSignal;
    s.receivedPowerDbm = rr.receivedPowerDbm;
    s.pathLossDb = rr.pathLossDb;
    s.delaySpreadNs = rr.delaySpreadNs;
    s.dopplerHz = sampleDoppler;
    s.servingTransmitterId = rr.servingTransmitterId;
    s.sinrDb = rr.sinrDb;
    s.interferencePowerDbm = rr.interferencePowerDbm;
    out.samples.push_back(std::move(s));
  }
  return out;
}

}  // namespace rftrace
