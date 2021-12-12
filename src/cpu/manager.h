#pragma once

#include "cpu.h"
#include "resmgr.h"
#include "worker.h"

#include <vector>

namespace cpu {

class CpuResourceManager : public ResourceManager {
 public:
  virtual bool Init() override;
  virtual void CreateWorkerThreads() override;
  virtual void Schedule(TimePoint time_point) override;

 protected:
  int jiffy_ms_;
  StatInfo stat_info_;
  TimePoint time_point_;
  std::vector<CpuWorkerContext> workers_;

  explicit CpuResourceManager(const Options &options);
  virtual void AdjustWorkerLoad(TimePoint time_point, int cpu_load) = 0;
};

}  // namespace cpu
