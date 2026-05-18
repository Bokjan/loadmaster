#include "worker.h"

#include <chrono>
#include <thread>

#include "critical_loop.h"

#include "core/constants.h"
#include "util/log.h"

namespace cpu {

void CpuWorkerContext::Loop(std::stop_token &stoken) {
  // Each scheduling period is `kScheduleIntervalMS` long.
  // Within a period, we busy-loop `load_set_` milliseconds, then sleep
  // the remainder of the period. `load_set_` therefore both represents
  // the per-core load percentage (0..100) AND the busy ms when the
  // period happens to equal 100 ms. Keep them as separate variables so
  // the relationship is explicit and the period can be changed safely.
  [[likely]] while (!stoken.stop_requested()) {
    const auto start = std::chrono::high_resolution_clock::now();
    const auto busy_target_ms = std::chrono::milliseconds(load_set_);
    for (;;) {
      cpu::CriticalLoop(base_loop_count_);
      const auto now = std::chrono::high_resolution_clock::now();
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
      if (elapsed >= busy_target_ms) {
        const auto period = std::chrono::milliseconds(kScheduleIntervalMS);
        const auto sleep_ms = (elapsed < period) ? (period - elapsed) : std::chrono::milliseconds(0);
        std::this_thread::sleep_for(sleep_ms);
        break;
      }
    }
  }
  LOG_TRACE("id=%d, stoken.stop_requested()=%d", id_, stoken.stop_requested());
}

}  // namespace cpu
