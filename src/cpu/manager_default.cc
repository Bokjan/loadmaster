#include "manager_default.h"

#include <algorithm>

#include "core/constants.h"
#include "core/options.h"
#include "util/log.h"

namespace cpu {

CpuResourceManagerDefault::CpuResourceManagerDefault(const core::Options &options)
    : CpuResourceManager(options) {}

bool CpuResourceManagerDefault::Init() {
  int count;
  if (options_.GetCpuCount() > 0) {
    count = options_.GetCpuCount();
  } else {
    count = (options_.GetCpuLoad() + (kCpuMaxLoadPerCore - 1)) / kCpuMaxLoadPerCore;
  }
  if (count > CoreCount()) {
    LOG_ERROR("CPU load `%d` needs %d CPU, have %u", options_.GetCpuLoad(), count, CoreCount());
    return false;
  }
  if (count <= 0) {
    return false;
  }

  return this->ConstructWorkerThreads(count);
}

void CpuResourceManagerDefault::AdjustWorkerLoad(TimePoint time_point, int system_load) {
  auto load_target = options_.GetCpuLoad();
  auto load_demand = this->CalculateLoadDemand(load_target);
  load_demand = std::clamp(load_demand, 0, load_target);
  LOG_TRACE("load_target=%d, avg_proc_load=%d, avg_sys_load=%d, load_demand=%d", load_target,
            this->GetProcessAverageLoad(), this->GetSystemAverageLoad(), load_demand);
  this->SetWorkerLoadWithTotalLoad(load_demand);
}

}  // namespace cpu
