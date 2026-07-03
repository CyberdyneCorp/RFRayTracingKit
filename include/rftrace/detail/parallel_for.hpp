#pragma once

// Deterministic, header-only parallel-for over an index range. `detail`-only;
// not part of the public API.
//
// Contract: parallelFor(n, threadCount, body) calls body(std::size_t i) EXACTLY
// once for every i in [0, n). The body MUST write only outputs indexed by i (a
// disjoint output slot) and share no mutable state — there is no reduction and
// no return value.
//
// Determinism (bit-for-bit, independent of thread count / OS scheduling): the
// work partition is a pure function of (n, T) — contiguous, index-based chunks
// with NO work-stealing and NO append-order output. Which thread runs index i
// never changes WHERE its result lands, and because each body writes only slot
// i there is no data race and no scheduling-dependent floating-point ordering.
//
// threadCount == 1 (or any caller that gates to 1 for a non-reentrant backend)
// takes the SERIAL branch: a plain ascending-index for-loop identical to the
// pre-change traversal, so it is trivially bit-for-bit unchanged.
//
// The utility owns no state (it just forks/joins), so it is itself reentrant.

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace rftrace {
namespace detail {

// Cap on worker threads regardless of hardware_concurrency / threadCount, to
// bound thread-creation overhead on many-core machines. Determinism does not
// depend on this value — any T partitions [0,n) into the same contiguous chunk
// rule; the cap only limits how many chunks are formed.
inline constexpr std::size_t kParallelForMaxThreads = 256;

/// Resolve the worker count purely from arguments (no timing): threadCount <= 0
/// => hardware_concurrency() (fallback 1); else threadCount. Clamped to [1, n]
/// and the cap. Returns 1 when the caller should run serially.
inline std::size_t resolveThreadCount(std::size_t n, int threadCount) {
  if (n == 0) return 1;
  std::size_t hw;
  if (threadCount <= 0) {
    const unsigned hc = std::thread::hardware_concurrency();
    hw = hc == 0 ? 1u : static_cast<std::size_t>(hc);
  } else {
    hw = static_cast<std::size_t>(threadCount);
  }
  return std::min({hw, n, kParallelForMaxThreads});
}

template <class Body>
void parallelFor(std::size_t n, int threadCount, Body&& body) {
  const std::size_t T = resolveThreadCount(n, threadCount);

  // Serial branch: exact pre-change ascending-index traversal.
  if (T <= 1 || n == 0) {
    for (std::size_t i = 0; i < n; ++i) body(i);
    return;
  }

  // Parallel branch: contiguous even split. Chunk c owns
  //   [ c*base + min(c,rem), (c+1)*base + min(c+1,rem) ).
  // This is a pure function of (n, T): every index lands in exactly one
  // contiguous chunk, chunks are disjoint and cover [0, n).
  const std::size_t base = n / T;
  const std::size_t rem = n % T;
  const auto chunkBegin = [&](std::size_t c) {
    return c * base + std::min(c, rem);
  };

  const auto runChunk = [&](std::size_t c) {
    const std::size_t begin = chunkBegin(c);
    const std::size_t end = chunkBegin(c + 1);
    for (std::size_t i = begin; i < end; ++i) body(i);
  };

  std::vector<std::thread> workers;
  workers.reserve(T - 1);
  for (std::size_t c = 1; c < T; ++c) workers.emplace_back(runChunk, c);
  runChunk(0);  // chunk 0 runs on the calling thread
  for (std::thread& w : workers) w.join();
}

}  // namespace detail
}  // namespace rftrace
