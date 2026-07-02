#include "rftrace/simulator.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "rftrace/rf/channel.hpp"
#include "rftrace/rf/free_space_path_loss.hpp"
#include "rftrace/rf/phase.hpp"
#include "rftrace/rf/reflection.hpp"

namespace rftrace {
namespace {

constexpr double kEps = 1e-4;

Vec3 mirrorPoint(const Vec3& p, const Vec3& planePoint, const Vec3& unitNormal) {
  const double d = (p - planePoint).dot(unitNormal);
  return p - 2.0 * d * unitNormal;
}

/// Intersection of the segment A→B with a plane; false if (near-)parallel or the
/// hit lies outside the open segment.
bool segmentPlane(const Vec3& a, const Vec3& b, const Vec3& planePoint,
                  const Vec3& n, Vec3& out) {
  const Vec3 ab = b - a;
  const double denom = n.dot(ab);
  if (std::abs(denom) < 1e-12) return false;
  const double t = n.dot(planePoint - a) / denom;
  if (t <= 1e-6 || t >= 1.0 - 1e-6) return false;
  out = a + t * ab;
  return true;
}

bool pointInTriangle(const Vec3& p, const Triangle& tri, double eps = 1e-6) {
  const Vec3 v0 = tri.v2 - tri.v0;
  const Vec3 v1 = tri.v1 - tri.v0;
  const Vec3 v2 = p - tri.v0;
  const double d00 = v0.dot(v0), d01 = v0.dot(v1), d02 = v0.dot(v2);
  const double d11 = v1.dot(v1), d12 = v1.dot(v2);
  const double denom = d00 * d11 - d01 * d01;
  if (std::abs(denom) < 1e-20) return false;
  const double u = (d11 * d02 - d01 * d12) / denom;
  const double v = (d00 * d12 - d01 * d02) / denom;
  return u >= -eps && v >= -eps && (u + v) <= 1.0 + eps;
}

double pathLength(const std::vector<Vec3>& pts) {
  double len = 0.0;
  for (std::size_t i = 1; i < pts.size(); ++i) len += (pts[i] - pts[i - 1]).norm();
  return len;
}

/// Fill the RF metrics of a path whose geometry (`points`) is already set.
void finishPath(RFPath& path, const Transmitter& tx, const Receiver& rx,
                double reflectionLossDb) {
  const auto& pts = path.points;
  const double len = pathLength(pts);
  const double fspl = rf::freeSpacePathLossDb(len, tx.frequencyHz);

  const Vec3 departDir = pts[1] - pts.front();          // tx → first hop
  const Vec3 arriveDir = pts[pts.size() - 2] - pts.back();  // rx → last hop
  const double gtx = tx.antenna.gainTowards(departDir);
  const double grx = rx.antenna.gainTowards(arriveDir);

  path.pathLossDb = fspl + reflectionLossDb;
  path.receivedPowerDbm = rf::receivedPowerDbm(tx.powerDbm, gtx, grx, path.pathLossDb);
  path.phaseRad = rf::propagationPhaseRad(len, tx.frequencyHz);
  path.delaySeconds = rf::propagationDelaySeconds(len);
}

/// Try to build a specular reflection path through the ordered triangle sequence
/// `seq` using the image method. Returns false if the geometry is invalid or any
/// segment is occluded.
bool buildReflection(const Scene& scene, const IBackend& backend,
                     const Transmitter& tx, const Receiver& rx,
                     const std::vector<int>& seq, RFPath& out) {
  const auto& tris = scene.triangles();

  // Successive images of the source across each plane.
  std::vector<Vec3> images;
  images.reserve(seq.size() + 1);
  images.push_back(tx.position);
  for (int idx : seq) {
    const Vec3 n = tris[idx].normal();
    if (n.norm() == 0.0) return false;
    images.push_back(mirrorPoint(images.back(), tris[idx].v0, n));
  }

  // Reflection points, resolved from the receiver backward.
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

  // Assemble the full path: tx, bounce points, rx.
  out.points.clear();
  out.points.push_back(tx.position);
  for (const Vec3& p : reflPts) out.points.push_back(p);
  out.points.push_back(rx.position);

  // Validate every segment is unoccluded and accumulate reflection loss.
  double reflLoss = 0.0;
  for (std::size_t i = 1; i < out.points.size(); ++i) {
    const Ray seg = segmentRay(out.points[i - 1], out.points[i], kEps);
    if (seg.tMax <= seg.tMin) return false;  // degenerate segment
    if (backend.occluded(seg)) return false;
  }
  for (int i = 0; i < k; ++i) {
    const Triangle& tri = tris[seq[i]];
    const Material& mat = scene.materialForTriangle(seq[i]);
    const Vec3 inDir = (out.points[i + 1] - out.points[i]).normalized();
    const double cosI = std::clamp(std::abs(inDir.dot(tri.normal())), 0.0, 1.0);
    const double incidence = std::acos(cosI);  // from normal
    reflLoss += rf::reflectionLossDb(mat, incidence, tx.polarization, tx.frequencyHz);
    out.materialHits.push_back(mat.name);
  }

  out.transmitterId = tx.id;
  out.receiverId = rx.id;
  out.type = PathType::Reflection;
  out.reflections = k;
  finishPath(out, tx, rx, reflLoss);
  return true;
}

/// Enumerate all ordered triangle sequences of length `depth` (no immediate
/// repeats) and append any that yield a valid reflection path.
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
    if (!seq.empty() && seq.back() == t) continue;  // no consecutive self-bounce
    seq.push_back(t);
    enumerateReflections(scene, backend, tx, rx, depth, seq, out);
    seq.pop_back();
  }
}

}  // namespace

RFResult Simulator::run(const Scene& scene) const {
  auto backend =
      makeBackend(settings_.backend, settings_.allowBackendFallback);
  backend->build(scene.triangles());

  RFResult result;
  result.simulationId = settings_.simulationId;
  result.frequencyHz =
      scene.transmitters().empty() ? 0.0 : scene.transmitters().front().frequencyHz;
  for (const Transmitter& tx : scene.transmitters())
    result.transmitters.push_back(
        {tx.id, tx.position, tx.frequencyHz, tx.powerDbm});

  for (const Receiver& rx : scene.receivers()) {
    ReceiverResult rr;
    rr.receiverId = rx.id;
    rr.position = rx.position;

    for (const Transmitter& tx : scene.transmitters()) {
      // Line of sight.
      const Ray los = segmentRay(tx.position, rx.position, kEps);
      if (los.tMax > los.tMin && !backend->occluded(los)) {
        RFPath path;
        path.transmitterId = tx.id;
        path.receiverId = rx.id;
        path.type = PathType::LOS;
        path.points = {tx.position, rx.position};
        finishPath(path, tx, rx, 0.0);
        rr.paths.push_back(std::move(path));
      }

      // Specular reflections up to maxReflections bounces.
      std::vector<int> seq;
      for (int depth = 1; depth <= settings_.maxReflections; ++depth)
        enumerateReflections(scene, *backend, tx, rx, depth, seq, rr.paths);
    }

    if (!rr.paths.empty()) {
      std::vector<double> powers, phases, delays;
      powers.reserve(rr.paths.size());
      for (const RFPath& p : rr.paths) {
        powers.push_back(p.receivedPowerDbm);
        phases.push_back(p.phaseRad);
        delays.push_back(p.delaySeconds);
      }
      rr.hasSignal = true;
      rr.receivedPowerDbm = settings_.coherent
                                ? rf::aggregateCoherentDbm(powers, phases)
                                : rf::aggregateIncoherentDbm(powers);
      rr.delaySpreadNs = rf::rmsDelaySpreadSeconds(powers, delays) * 1e9;

      // Report the strongest path's loss/phase as the receiver-level figure.
      const auto strongest = std::max_element(
          rr.paths.begin(), rr.paths.end(), [](const RFPath& a, const RFPath& b) {
            return a.receivedPowerDbm < b.receivedPowerDbm;
          });
      rr.pathLossDb = strongest->pathLossDb;
      rr.phaseRad = strongest->phaseRad;
    }

    result.receivers.push_back(std::move(rr));
  }

  return result;
}

}  // namespace rftrace
