#include "rftrace/simulator.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "rftrace/detail/propagation.hpp"
#include "rftrace/rf/channel.hpp"
#include "rftrace/rf/reflection.hpp"

namespace rftrace {
namespace detail {

std::optional<RFPath> losPath(const IBackend& backend, const Transmitter& tx,
                              const Receiver& rx) {
  const Ray los = segmentRay(tx.position, rx.position, kEps);
  if (los.tMax <= los.tMin) return std::nullopt;
  if (backend.occluded(los)) return std::nullopt;

  RFPath p;
  p.transmitterId = tx.id;
  p.receiverId = rx.id;
  p.type = PathType::LOS;
  p.points = {tx.position, rx.position};
  finishPath(p, tx, rx, 0.0);
  return p;
}

namespace {

/// Build one specular reflection path through the ordered triangle sequence
/// `seq` using the image method. Returns false on invalid geometry/occlusion.
bool buildReflection(const Scene& scene, const IBackend& backend,
                     const Transmitter& tx, const Receiver& rx,
                     const std::vector<int>& seq, RFPath& out) {
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
    if (backend.occluded(seg)) return false;
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
  finishPath(out, tx, rx, reflLoss);
  return true;
}

void enumerateReflections(const Scene& scene, const IBackend& backend,
                          const Transmitter& tx, const Receiver& rx, int depth,
                          std::vector<int>& seq, std::vector<RFPath>& out) {
  if (static_cast<int>(seq.size()) == depth) {
    RFPath path;
    if (buildReflection(scene, backend, tx, rx, seq, path))
      out.push_back(std::move(path));
    return;
  }
  const int n = static_cast<int>(scene.triangles().size());
  for (int t = 0; t < n; ++t) {
    if (!seq.empty() && seq.back() == t) continue;
    seq.push_back(t);
    enumerateReflections(scene, backend, tx, rx, depth, seq, out);
    seq.pop_back();
  }
}

}  // namespace

std::vector<RFPath> imageMethodReflections(const Scene& scene,
                                           const IBackend& backend,
                                           const Transmitter& tx,
                                           const Receiver& rx,
                                           int maxReflections) {
  std::vector<RFPath> paths;
  std::vector<int> seq;
  for (int depth = 1; depth <= maxReflections; ++depth)
    enumerateReflections(scene, backend, tx, rx, depth, seq, paths);
  return paths;
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

  const auto& receivers = scene.receivers();
  std::vector<ReceiverResult> rrs(receivers.size());
  for (std::size_t i = 0; i < receivers.size(); ++i) {
    rrs[i].receiverId = receivers[i].id;
    rrs[i].position = receivers[i].position;
  }

  // LOS is deterministic in both modes.
  for (std::size_t i = 0; i < receivers.size(); ++i)
    for (const Transmitter& tx : scene.transmitters())
      if (auto los = detail::losPath(*backend, tx, receivers[i]))
        rrs[i].paths.push_back(std::move(*los));

  if (settings_.mode == PropagationMode::ImageMethod) {
    for (std::size_t i = 0; i < receivers.size(); ++i)
      for (const Transmitter& tx : scene.transmitters()) {
        auto refl = detail::imageMethodReflections(scene, *backend, tx,
                                                   receivers[i],
                                                   settings_.maxReflections);
        for (auto& p : refl) rrs[i].paths.push_back(std::move(p));
      }
  } else {  // RayLaunch
    for (const Transmitter& tx : scene.transmitters()) {
      auto perRx = detail::rayLaunch(scene, *backend, tx, receivers, settings_);
      for (std::size_t i = 0; i < receivers.size(); ++i)
        for (auto& p : perRx[i]) rrs[i].paths.push_back(std::move(p));
    }
  }

  for (auto& rr : rrs) {
    detail::aggregate(rr, settings_.coherent);
    result.receivers.push_back(std::move(rr));
  }
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

  for (int row = 0; row < grid.rows; ++row) {
    for (int col = 0; col < grid.cols; ++col) {
      Receiver rx;
      rx.id = "cell";
      rx.position = grid.cellCenter(row, col);

      ReceiverResult rr;
      rr.receiverId = rx.id;
      rr.position = rx.position;
      for (const Transmitter& tx : scene.transmitters()) {
        if (auto los = detail::losPath(*backend, tx, rx))
          rr.paths.push_back(std::move(*los));
        auto refl = detail::imageMethodReflections(scene, *backend, tx, rx,
                                                   settings_.maxReflections);
        for (auto& p : refl) rr.paths.push_back(std::move(p));
      }
      detail::aggregate(rr, settings_.coherent);

      if (rr.hasSignal) {
        const int idx = row * grid.cols + col;
        cov.powerDbm[idx] = rr.receivedPowerDbm;
        cov.pathLossDb[idx] = rr.pathLossDb;
      }
    }
  }
  return cov;
}

}  // namespace rftrace
