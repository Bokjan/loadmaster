#pragma once

#include <vector>

#include "stat.h"
#include "worker.h"

#include "core/resource_manager.h"
#include "util/proc_stat.h"
#include "util/rolling_sampler.h"

namespace cpu {

class CpuResourceManager : public core::ResourceManager {
 public:
  virtual bool Init() = 0;
  virtual void CreateWorkerThreads() override final;
  virtual void RequestWorkerThreadsStop() override final;
  virtual void JoinWorkerThreads() override final;
  virtual void Schedule(TimePoint time_point) override final;

 protected:
  explicit CpuResourceManager(const core::Options &options);
  virtual void AdjustWorkerLoad(TimePoint time_point, int system_load) = 0;

  bool ConstructWorkerThreads(int count);
  void SetWorkerLoadWithTotalLoad(int total_load);
  int CalculateLoadDemand(int target);

  TimePoint GetLastScheduling() const { return last_scheduling_; }
  int GetSystemAverageLoad() const { return system_sampler_.GetMean(); }
  int GetProcessAverageLoad() const { return process_sampler_.GetMean(); }

 private:
  int jiffy_ms_;
  TimePoint last_scheduling_;
  std::vector<CpuWorkerContext> workers_;
  int base_loop_count_;
  CpuStatInfo cpu_stat_;
  util::RollingSampler<int> system_sampler_;
  util::ProcStat proc_stat_;
  util::RollingSampler<int> process_sampler_;

  int FindAccurateBaseLoopCount(int max_iteration);
  void UpdateProcStat(TimePoint time_point);
};

}  // namespace cpu
