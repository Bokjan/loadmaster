#include "manager_default.h"

#include "constants.h"
#include "options.h"

namespace cpu {

CpuResourceManagerDefault::CpuResourceManagerDefault(const Options &options)
    : CpuResourceManager(options) {}

void CpuResourceManagerDefault::AdjustWorkerLoad(TimePoint time_point, int cpu_load) {
  int core_target = 0;
  int last_wasted_load = 0;
  for (const auto &ctx : workers_) {
    last_wasted_load += ctx.load_set_;
  }

  do {
    // set to 0 if heavy loaded now
    if (cpu_load >= kCpuPauseLoopLoadPercentage * Count()) {
      core_target = 0;
      break;
    }
    // maximum idle logic
    auto idle_load = (cpu::Count() * kCpuMaxLoadPerCore) - cpu_load - last_wasted_load;
    if (idle_load > options_.cpu_load_) {
      idle_load = options_.cpu_load_;
    }
    core_target = idle_load / static_cast<int>(workers_.size());
  } while (false);

  for (auto &ctx : workers_) {
    ctx.load_set_ = core_target;
  }
}

}  // namespace cpu
