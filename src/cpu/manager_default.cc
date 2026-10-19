#include "manager_default.h"

#include "constants.h"

#include "core/options.h"
#include "util/log.h"

namespace cpu {

CpuResourceManagerDefault::CpuResourceManagerDefault(const core::Options &options)
    : CpuResourceManager(options) {}

bool CpuResourceManagerDefault::Init() {
  if (options_.GetCpuLoad() <= 0) {
    LOG_INFO("CPU module disabled (-l 0)");
    return false;
  }
  int count;
  if (options_.GetCpuCount() > 0) {
    count = options_.GetCpuCount();
  } else {
    count = (options_.GetCpuLoad() + (kCpuMaxLoadPerCore - 1)) / kCpuMaxLoadPerCore;
  }
  if (count <= 0) {
    return false;
  }
  if (count > CoreCount()) {
    LOG_ERROR("CPU load `%d` needs %d CPU, have %d", options_.GetCpuLoad(), count, CoreCount());
    return false;
  }
  return this->ConstructWorkerThreads(count);
}

void CpuResourceManagerDefault::AdjustWorkerLoad([[maybe_unused]] TimePoint time_point,
                                                 [[maybe_unused]] int system_load) {
  auto load_target = options_.GetCpuLoad();
  auto load_demand = this->CalculateLoadDemand(load_target);
  LOG_TRACE("load_target=%d, avg_proc_load=%.1f, avg_sys_load=%.1f, load_demand=%d", load_target,
            this->GetProcessAverageLoad(), this->GetSystemAverageLoad(), load_demand);
  this->SetWorkerLoadWithTotalLoad(load_demand);
}

}  // namespace cpu
