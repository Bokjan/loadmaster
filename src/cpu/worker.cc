#include "worker.h"

#include "critical_loop.h"
#include "stat.h"

#include "core/constants.h"
#include "core/runtime.h"
#include "util/log.h"

namespace cpu {

void CpuWorkerContext::Loop(std::stop_token &stoken) {
  // 100ms is a scheduling period
  [[likely]] while (!stoken.stop_requested()) {
    auto start = std::chrono::high_resolution_clock::now();
    for (;;) {
      cpu::CriticalLoop(base_loop_count_);
      auto now = std::chrono::high_resolution_clock::now();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
      if (ms.count() >= load_set_) {
        auto sleep_ms = kCpuMaxLoadPerCore - ms.count();
        if (sleep_ms < 0) {
          sleep_ms = 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        break;
      }
    }
  }
  LOG_TRACE("id=%d, stoken.stop_requested()=%d", id_, stoken.stop_requested());
}

}  // namespace cpu
