#pragma once

#include "cpu.h"
#include "resmgr.h"
#include "worker.h"

#include <vector>

namespace cpu {

class CpuResourceManager : public ResourceManager {
 public:
  virtual void InitWithOptions(const Options &options);
  virtual void CreateWorkerThreads();

 protected:
  int jiffy_ms_;
  std::vector<CpuWorkerContext> workers_;

  explicit CpuResourceManager(const Options &options);
  virtual void AdjustWorkerLoad(int cpu_load) = 0;
};

class CpuResourceManagerSimple final : public CpuResourceManager {
 public:
  explicit CpuResourceManagerSimple(const Options &options);
  void Schedule(TimePoint time_point);

 protected:
  void AdjustWorkerLoad(int cpu_load);

 private:
  StatInfo stat_info_;
  TimePoint time_point_;
};

}  // namespace cpu
