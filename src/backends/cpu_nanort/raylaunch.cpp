#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "rftrace/detail/batch_query.hpp"
#include "rftrace/detail/propagation.hpp"
#include "rftrace/rf/reflection.hpp"

namespace rftrace::detail {
namespace {

/// Deterministic near-uniform sphere direction (Fibonacci sphere). `seed` adds a
/// reproducible azimuth offset so different seeds explore different directions.
Vec3 fibonacciDirection(int i, int n, std::uint64_t seed) {
  const double golden = constants::pi * (3.0 - std::sqrt(5.0));
  const double z = 1.0 - 2.0 * (i + 0.5) / n;
  const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
  const double theta = golden * i + 0.001 * static_cast<double>(seed % 6283);
  return Vec3{r * std::cos(theta), r * std::sin(theta), z};
}

Vec3 reflectDir(const Vec3& d, const Vec3& n) { return d - 2.0 * d.dot(n) * n; }

/// Reflection loss (dB) and material names for a captured polyline `points`
/// bouncing off the triangles in `sig`.
double reflectionLossFor(const Scene& scene, const Transmitter& tx,
                         const std::vector<Vec3>& points,
                         const std::vector<int>& sig,
                         std::vector<std::string>& materialHits) {
  double loss = 0.0;
  for (std::size_t i = 0; i < sig.size(); ++i) {
    const Triangle& tri = scene.triangles()[sig[i]];
    const Material& mat = scene.materialForTriangle(sig[i]);
    const Vec3 inDir = (points[i + 1] - points[i]).normalized();
    const double cosI = std::clamp(std::abs(inDir.dot(tri.normal())), 0.0, 1.0);
    loss += rf::reflectionLossDb(mat, std::acos(cosI), tx.polarization,
                                 tx.frequencyHz);
    materialHits.push_back(mat.name);
  }
  return loss;
}

/// Per-bounce walk state recorded during batched traversal so the ray-major
/// capture replay needs no ray query. Holds exactly what the capture/dedup and
/// reflect steps consume: the capture segment endpoints (`origin`, `segEnd`),
/// whether the segment hit geometry, and the reflecting triangle + hit point.
struct SegRecord {
  Vec3 origin;       ///< segment start (feeds distancePointToSegmentSq)
  Vec3 dir;          ///< segment direction (traversal-only; not read by replay)
  Vec3 segEnd;       ///< origin + dir * (hit.valid ? hit.t : kFar)
  bool hitValid;     ///< hit.valid; drives reflect-vs-terminate in replay
  int hitTriangle;   ///< hit.triangle appended to sig; -1 when !hitValid
  Vec3 hitPoint;     ///< ray.at(hit.t) appended to bounces; segEnd when !hitValid
};

/// Live-ray traversal state carried across bounces in the batched wavefront.
struct Live {
  int ray;                   ///< ray index (into walk[])
  Vec3 origin;               ///< current segment origin
  Vec3 dir;                  ///< current segment direction (normalized)
  std::vector<Vec3> bounces; ///< reflection points so far (for the power floor)
};

constexpr double kFar = 1e6;

/// Capture reflected rays passing near a receiver on the segment [segStart,
/// segEnd] with the accumulated `bounces`/`sig`. Verbatim of the original inline
/// capture/dedup body (dedup by reflecting-surface signature, keep-strongest).
void captureSegment(const Scene& scene, const Transmitter& tx,
                    const std::vector<Receiver>& receivers,
                    const PropagationContext* ctx, double capture2,
                    const std::vector<Vec3>& bounces, const std::vector<int>& sig,
                    const Vec3& segStart, const Vec3& segEnd,
                    std::vector<std::vector<RFPath>>& out,
                    std::vector<std::unordered_map<std::string, int>>& seen) {
  const int nRx = static_cast<int>(receivers.size());
  for (int r = 0; r < nRx; ++r) {
    if (distancePointToSegmentSq(receivers[r].position, segStart, segEnd) >
        capture2)
      continue;

    RFPath path;
    path.points.push_back(tx.position);
    for (const Vec3& b : bounces) path.points.push_back(b);
    path.points.push_back(receivers[r].position);

    const double reflLoss = reflectionLossFor(scene, tx, path.points, sig,
                                              path.materialHits);
    path.transmitterId = tx.id;
    path.receiverId = receivers[r].id;
    path.type = PathType::Reflection;
    path.reflections = static_cast<int>(sig.size());
    finishPath(path, tx, receivers[r], reflLoss, ctx);

    // Dedup by reflecting-surface signature; keep the strongest.
    std::string key;
    for (int s : sig) key += std::to_string(s) + "-";
    auto it = seen[r].find(key);
    if (it == seen[r].end()) {
      seen[r][key] = static_cast<int>(out[r].size());
      out[r].push_back(std::move(path));
    } else if (path.receivedPowerDbm > out[r][it->second].receivedPowerDbm) {
      out[r][it->second] = std::move(path);
    }
  }
}

/// Advance one live ray past its recorded hit: reflect and apply the power-floor
/// termination. Returns true if the ray survives to the next bounce. Mirrors the
/// original reflect (lines 111-117) and power-floor (lines 121-125) verbatim.
bool advanceRay(const Scene& scene, const Transmitter& tx,
                const SimulationSettings& settings, const SegRecord& rec,
                Live& L) {
  Vec3 n = scene.triangles()[rec.hitTriangle].normal();
  if (L.dir.dot(n) > 0.0) n = -n;  // face the incoming ray
  L.bounces.push_back(rec.hitPoint);
  L.dir = reflectDir(L.dir, n).normalized();
  L.origin = rec.hitPoint;

  // Power-floor termination: stop if even an immediate capture would be below
  // the floor.
  double partialLen = (L.bounces.front() - tx.position).norm();
  for (std::size_t b = 1; b < L.bounces.size(); ++b)
    partialLen += (L.bounces[b] - L.bounces[b - 1]).norm();
  const double approxLoss = rf::freeSpacePathLossDb(partialLen, tx.frequencyHz);
  return tx.powerDbm - approxLoss >= settings.powerFloorDbm;
}

}  // namespace

std::vector<std::vector<RFPath>> rayLaunch(const Scene& scene,
                                           const IBackend& backend,
                                           const Transmitter& tx,
                                           const std::vector<Receiver>& receivers,
                                           const SimulationSettings& settings,
                                           const PropagationContext* ctx) {
  const int nRx = static_cast<int>(receivers.size());
  std::vector<std::vector<RFPath>> out(nRx);
  // Per receiver: reflecting-surface signature -> index into out[r] (dedup).
  std::vector<std::unordered_map<std::string, int>> seen(nRx);

  const int nRays = std::max(0, settings.raysPerTransmitter);
  const double capture2 = settings.captureRadius * settings.captureRadius;

  // --- Phase A: batched wavefront traversal (records, no captures) ----------
  // Every ray's walked segments land in walk[ray] in bounce order. The order of
  // rays in the batch never touches out[r]; captures are replayed ray-major.
  std::vector<std::vector<SegRecord>> walk(nRays);
  std::vector<Live> live;
  live.reserve(nRays);
  for (int i = 0; i < nRays; ++i)
    live.push_back(Live{i, tx.position,
                        fibonacciDirection(i, nRays, settings.seed).normalized(),
                        {}});

  BatchQuery bq;
  for (int bounce = 0; bounce <= settings.maxReflections && !live.empty();
       ++bounce) {
    bq.clear();
    for (const Live& L : live)
      bq.add(Ray(L.origin, L.dir, kEps,
                 std::numeric_limits<double>::infinity()));
    bq.runClosestHit(backend);  // single batched dispatch (the GPU win)

    std::vector<Live> next;
    next.reserve(live.size());
    for (std::size_t j = 0; j < live.size(); ++j) {
      Live& L = live[j];
      const Hit hit = bq.hit(j);
      const Ray ray(L.origin, L.dir, kEps,
                    std::numeric_limits<double>::infinity());
      const double segLen = hit.valid ? hit.t : kFar;

      SegRecord rec;
      rec.origin = L.origin;
      rec.dir = L.dir;
      rec.segEnd = L.origin + L.dir * segLen;
      rec.hitValid = hit.valid;
      rec.hitTriangle = hit.valid ? hit.triangle : -1;
      rec.hitPoint = hit.valid ? ray.at(hit.t) : rec.segEnd;
      walk[L.ray].push_back(rec);

      if (!hit.valid) continue;  // ray left the scene (mirrors break at 108)
      if (advanceRay(scene, tx, settings, rec, L)) next.push_back(std::move(L));
    }
    live = std::move(next);
  }

  // --- Phase B: ray-major capture replay (order-sensitive accumulation) -----
  // Reconstruct bounces/sig per ray in bounce order and run the exact capture
  // body, so out[r]/seen[r] end byte-identical to the sequential walk.
  for (int i = 0; i < nRays; ++i) {
    std::vector<Vec3> bounces;
    std::vector<int> sig;
    for (const SegRecord& rec : walk[i]) {
      if (!bounces.empty())
        captureSegment(scene, tx, receivers, ctx, capture2, bounces, sig,
                       rec.origin, rec.segEnd, out, seen);
      if (rec.hitValid) {
        bounces.push_back(rec.hitPoint);
        sig.push_back(rec.hitTriangle);
      }
    }
  }
  return out;
}

std::vector<std::vector<RFPath>> rayLaunchReference(
    const Scene& scene, const IBackend& backend, const Transmitter& tx,
    const std::vector<Receiver>& receivers, const SimulationSettings& settings,
    const PropagationContext* ctx) {
  const int nRx = static_cast<int>(receivers.size());
  std::vector<std::vector<RFPath>> out(nRx);
  // Per receiver: reflecting-surface signature -> index into out[r] (dedup).
  std::vector<std::unordered_map<std::string, int>> seen(nRx);

  const int nRays = std::max(0, settings.raysPerTransmitter);
  const double capture2 = settings.captureRadius * settings.captureRadius;

  for (int i = 0; i < nRays; ++i) {
    Vec3 origin = tx.position;
    Vec3 dir = fibonacciDirection(i, nRays, settings.seed).normalized();
    std::vector<Vec3> bounces;  // reflection points so far
    std::vector<int> sig;       // triangle indices so far

    for (int bounce = 0; bounce <= settings.maxReflections; ++bounce) {
      const Ray ray(origin, dir, kEps,
                    std::numeric_limits<double>::infinity());
      const Hit hit = backend.closestHit(ray);
      const double segLen = hit.valid ? hit.t : kFar;
      const Vec3 segEnd = origin + dir * segLen;

      // Capture reflected rays passing near a receiver (LOS is added elsewhere).
      if (!bounces.empty()) {
        for (int r = 0; r < nRx; ++r) {
          if (distancePointToSegmentSq(receivers[r].position, origin, segEnd) >
              capture2)
            continue;

          RFPath path;
          path.points.push_back(tx.position);
          for (const Vec3& b : bounces) path.points.push_back(b);
          path.points.push_back(receivers[r].position);

          const double reflLoss = reflectionLossFor(scene, tx, path.points, sig,
                                                    path.materialHits);
          path.transmitterId = tx.id;
          path.receiverId = receivers[r].id;
          path.type = PathType::Reflection;
          path.reflections = static_cast<int>(sig.size());
          finishPath(path, tx, receivers[r], reflLoss, ctx);

          // Dedup by reflecting-surface signature; keep the strongest.
          std::string key;
          for (int s : sig) key += std::to_string(s) + "-";
          auto it = seen[r].find(key);
          if (it == seen[r].end()) {
            seen[r][key] = static_cast<int>(out[r].size());
            out[r].push_back(std::move(path));
          } else if (path.receivedPowerDbm >
                     out[r][it->second].receivedPowerDbm) {
            out[r][it->second] = std::move(path);
          }
        }
      }

      if (!hit.valid) break;  // ray left the scene

      // Reflect and continue.
      Vec3 n = scene.triangles()[hit.triangle].normal();
      if (dir.dot(n) > 0.0) n = -n;  // face the incoming ray
      const Vec3 hitPoint = ray.at(hit.t);
      bounces.push_back(hitPoint);
      sig.push_back(hit.triangle);
      dir = reflectDir(dir, n).normalized();
      origin = hitPoint;

      // Power-floor termination: stop if even an immediate capture would be
      // below the floor.
      double partialLen = (bounces.front() - tx.position).norm();
      for (std::size_t b = 1; b < bounces.size(); ++b)
        partialLen += (bounces[b] - bounces[b - 1]).norm();
      const double approxLoss = rf::freeSpacePathLossDb(partialLen, tx.frequencyHz);
      if (tx.powerDbm - approxLoss < settings.powerFloorDbm) break;
    }
  }
  return out;
}

}  // namespace rftrace::detail
