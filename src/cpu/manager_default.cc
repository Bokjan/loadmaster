#include "manager_default.h"

#include "constants.h"
#include "options.h"

namespace cpu {

CpuResourceManagerDefault::CpuResourceManagerDefault(const Options &options)
    : CpuResourceManager(options) {}

void CpuResourceManagerDefault::AdjustWorkerLoad(TimePoint time_point, int cpu_load) {
  int core_target = 0;

  do {
    auto load_demand = options_.CpuLoad() - cpu_load;
    if (load_demand <= 0) {
      break;
    }
    core_target = load_demand / static_cast<int>(workers_.size());
  } while (false);

  for (auto &ctx : workers_) {
    ctx.SetLoadSet(core_target);
  }
}

}  // namespace cpu
