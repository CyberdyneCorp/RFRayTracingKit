#include "rftrace/simulator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "rftrace/cell_planning.hpp"
#include "rftrace/detail/propagation.hpp"
#include "rftrace/rf/channel.hpp"
#include "rftrace/rf/diffraction.hpp"
#include "rftrace/rf/reflection.hpp"

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

  RFPath p;
  p.transmitterId = tx.id;
  p.receiverId = rx.id;
  p.type = PathType::LOS;
  p.points = {tx.position, rx.position};
  finishPath(p, tx, rx, 0.0, ctx);
  return p;
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
  out.materialHits.clear();
  for (int i = 0; i < k; ++i) {
    const Triangle& tri = tris[seq[i]];
    const Material& mat = scene.materialForTriangle(seq[i]);
    const Vec3 inDir = (out.points[i + 1] - out.points[i]).normalized();
    const double cosI = std::clamp(std::abs(inDir.dot(tri.normal())), 0.0, 1.0);
    reflLoss += rf::reflectionLossDb(mat, std::acos(cosI), tx.polarization,
                                     tx.frequencyHz);
    out.materialHits.push_back(mat.name);
  }

  out.transmitterId = tx.id;
  out.receiverId = rx.id;
  out.type = PathType::Reflection;
  out.reflections = k;
  finishPath(out, tx, rx, reflLoss, ctx);
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
  finishPath(p, tx, rx, rf::knifeEdgeLossDb(best.v), ctx);
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

RFResult Simulator::run(const Scene& scene) const {
  auto backend = makeBackend(settings_.backend, settings_.allowBackendFallback);
  backend->build(scene.triangles());

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
  // enabled, attempt a single dominant knife-edge detour instead.
  for (std::size_t i = 0; i < receivers.size(); ++i)
    for (const Transmitter& tx : scene.transmitters()) {
      if (auto los = detail::losPath(*backend, tx, receivers[i], &ctx)) {
        rrs[i].paths.push_back(std::move(*los));
      } else if (settings_.enableDiffraction) {
        if (auto dif = detail::diffractionPath(scene, *backend, tx, receivers[i],
                                               tx.frequencyHz, &ctx))
          rrs[i].paths.push_back(std::move(*dif));
      }
    }

  if (settings_.mode == PropagationMode::ImageMethod) {
    for (std::size_t i = 0; i < receivers.size(); ++i)
      for (const Transmitter& tx : scene.transmitters()) {
        auto refl = detail::imageMethodReflections(
            scene, *backend, tx, receivers[i], settings_.maxReflections, &ctx);
        for (auto& p : refl) rrs[i].paths.push_back(std::move(p));
      }
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

CoverageResult Simulator::runCoverage(const Scene& scene,
                                      const CoverageGrid& grid) const {
  auto backend = makeBackend(settings_.backend, settings_.allowBackendFallback);
  backend->build(scene.triangles());

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

  const detail::PropagationContext ctx{settings_, scene, *backend,
                                       cov.frequencyHz};

  for (int row = 0; row < grid.rows; ++row) {
    for (int col = 0; col < grid.cols; ++col) {
      Receiver rx;
      rx.id = "cell";
      rx.position = grid.cellCenter(row, col);

      ReceiverResult rr;
      rr.receiverId = rx.id;
      rr.position = rx.position;
      for (const Transmitter& tx : scene.transmitters()) {
        if (auto los = detail::losPath(*backend, tx, rx, &ctx))
          rr.paths.push_back(std::move(*los));
        auto refl = detail::imageMethodReflections(
            scene, *backend, tx, rx, settings_.maxReflections, &ctx);
        for (auto& p : refl) rr.paths.push_back(std::move(p));
      }
      detail::aggregate(rr, settings_.coherent);

      if (rr.hasSignal) {
        const int idx = row * grid.cols + col;
        cov.powerDbm[idx] = rr.receivedPowerDbm;
        cov.pathLossDb[idx] = rr.pathLossDb;
        if (settings_.enableSinr)
          cov.sinrDb[idx] = computeSinr(rr, settings_).sinrDb;
      }
    }
  }
  return cov;
}

RouteResult Simulator::runRoute(const Scene& scene, const Route& route) const {
  auto backend = makeBackend(settings_.backend, settings_.allowBackendFallback);
  backend->build(scene.triangles());

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

    RouteSample s;
    s.index = static_cast<int>(i);
    s.distanceMeters = points[i].distanceMeters;
    s.position = points[i].position;
    s.hasSignal = rr.hasSignal;
    s.receivedPowerDbm = rr.receivedPowerDbm;
    s.pathLossDb = rr.pathLossDb;
    s.delaySpreadNs = rr.delaySpreadNs;
    s.servingTransmitterId = rr.servingTransmitterId;
    s.sinrDb = rr.sinrDb;
    s.interferencePowerDbm = rr.interferencePowerDbm;
    out.samples.push_back(std::move(s));
  }
  return out;
}

}  // namespace rftrace
