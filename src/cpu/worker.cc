#include "worker.h"

#include "constants.h"
#include "global.h"
#include "runtime.h"
#include "util/log.h"

namespace cpu {

void EmptyLoop(int count);

void WorkerThreadProcedure(Runtime &runtime, ThreadContext &ctx) {
  // 100ms is a scheduling period
  while (global::keep_loop) {
    auto start = std::chrono::high_resolution_clock::now();
    for (;;) {
      EmptyLoop(kBaseLoopCount);
      auto now = std::chrono::high_resolution_clock::now();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
      if (ms.count() >= ctx.load_set_) {
        auto sleep_ms = kMaxLoadPerCore - ms.count();
        if (sleep_ms < 0) {
          sleep_ms = 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        break;
      }
    }
  }
  runtime.wg_.Done();
}

void EmptyLoop(int count) {
  for (int i = 0; i < count; ++i) {
    continue;
  }
}

}
