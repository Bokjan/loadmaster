#pragma once

#include "cpu.h"
#include "resmgr.h"
#include "worker.h"

#include <vector>

namespace cpu {

class CpuResourceManager : public ResourceManager {
 public:
  virtual bool Init();
  virtual void CreateWorkerThreads();
  virtual void Schedule(TimePoint time_point);

 protected:
  int jiffy_ms_;
  StatInfo stat_info_;
  TimePoint time_point_;
  std::vector<CpuWorkerContext> workers_;

  explicit CpuResourceManager(const Options &options);
  virtual void AdjustWorkerLoad(int cpu_load) = 0;
};

class CpuResourceManagerDefault final : public CpuResourceManager {
 public:
  explicit CpuResourceManagerDefault(const Options &options);

 protected:
  void AdjustWorkerLoad(int cpu_load);
};

}  // namespace cpu
