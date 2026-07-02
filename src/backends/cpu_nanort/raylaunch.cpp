#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

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
  constexpr double kFar = 1e6;

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
