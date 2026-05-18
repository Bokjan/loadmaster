#include "critical_loop.h"

#include <cstdint>
#include <random>

namespace cpu {

// Busy-work routine used by CPU workers to actually burn cycles.
//
// Notes:
//   * `count` must be > 0 (callers guarantee this via the loop-count probe).
//   * Use unsigned arithmetic so wrap-around is well-defined; signed overflow
//     would be undefined behavior.
//   * Cache the PRNG and avoid constructing a new distribution on every call.
uint32_t CriticalLoop(int count) {
  if (count <= 0) {
    return 0u;
  }
  thread_local std::mt19937 gen(std::random_device{}());
  // Pick two pseudo-random unsigned seeds; subsequent multiplies are
  // performed in unsigned space (defined modular wrap-around).
  uint32_t factor = static_cast<uint32_t>(gen());
  thread_local uint32_t val = static_cast<uint32_t>(gen());
  for (int i = 0; i < count; ++i) {
    val *= factor;
  }
  return val;
}

}  // namespace cpu
