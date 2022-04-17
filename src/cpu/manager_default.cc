#include "manager_default.h"

#include "constants.h"
#include "options.h"
#include "util/algorithm.h"
#include "util/log.h"

namespace cpu {

CpuResourceManagerDefault::CpuResourceManagerDefault(const Options &options)
    : CpuResourceManager(options) {}

bool CpuResourceManagerDefault::Init() {
  int count;
  if (options_.CpuCount() > 0) {
    count = options_.CpuCount();
  } else {
    count = (options_.CpuLoad() + (kCpuMaxLoadPerCore - 1)) / kCpuMaxLoadPerCore;
  }
  if (count > Count()) {
    LOG_ERROR("CPU load `%d` needs %d CPU, have %d", options_.CpuLoad(), count, Count());
    return false;
  }
  if (count <= 0) {
    return false;
  }

  return this->ConstructWorkerThreads(count);
}

void CpuResourceManagerDefault::AdjustWorkerLoad(TimePoint time_point, int system_load) {
  auto load_target = options_.CpuLoad();
  auto load_demand = this->CalculateLoadDemand(load_target);
  load_demand = util::Clamp(load_demand, 0, load_target);
  LOG_TRACE("load_target=%d, avg_proc_load=%d, avg_sys_load=%d, load_demand=%d", load_target,
            this->GetProcessAverageLoad(), this->GetSystemAverageLoad(), load_demand);
  this->SetWorkerLoadWithTotalLoad(load_demand);
}

}  // namespace cpu
