#include "master.h"

#include "cpu/cpu.h"
#include "options.h"
#include "runtime.h"
#include "util/log.h"
#include "worker.h"

namespace cpu {

void InitThreads(const Options &options, Runtime &runtime) {
  int count;
  if (options.count_ > 0) {
    count = options.count_;
  } else {
    count = (options.load_ + (kMaxLoadPerCore - 1)) / kMaxLoadPerCore;
  }
  int avg_load = (options.load_ + (count - 1)) / count;
  if (count > Count()) {
    LOG_E("CPU load `%d` needs %d CPU, have %d; use %d", options.load_, count, Count(), Count());
    count = Count();
    avg_load = kMaxLoadPerCore;
  }
  for (int i = 0; i < count; ++i) {
    ThreadContext ctx;
    ctx.id_ = i;
    ctx.load_max_ = avg_load;
    runtime.cpu_threads_.emplace_back(std::move(ctx));
  }
}

void CreateWorkerThreads(Runtime &runtime) {
  for (auto &ctx : runtime.cpu_threads_) {
    ctx.thread_ = std::thread(WorkerThreadProcedure, std::ref(runtime), std::ref(ctx));
    runtime.wg_.Incr();
    ctx.thread_.detach();
  }
}

}  // namespace cpu
