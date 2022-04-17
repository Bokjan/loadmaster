#pragma once

#include "cpu.h"
#include "resmgr.h"
#include "util/proc_stat.h"
#include "worker.h"

#include <vector>

namespace cpu {

class CpuResourceManager : public ResourceManager {
 public:
  virtual bool Init() = 0;
  virtual void CreateWorkerThreads() override final;
  virtual void Schedule(TimePoint time_point) override final;

 protected:
  explicit CpuResourceManager(const Options &options);
  virtual void AdjustWorkerLoad(TimePoint time_point, int system_load) = 0;

  bool ConstructWorkerThreads(int count);
  void SetWorkerLoadWithTotalLoad(int total_load);
  int CalculateLoadDemand(int target);

  int GetSystemAverageLoad() const { return system_average_load_; }
  int GetProcessAverageLoad() const { return process_average_load_; }
  TimePoint GetLastScheduling() const { return last_scheduling_; }

 private:
  int jiffy_ms_;
  TimePoint last_scheduling_;
  std::vector<CpuWorkerContext> workers_;
  int base_loop_count_;
  CpuStatInfo cpu_stat_;
  int system_average_load_;
  int system_load_sampling_index_;
  std::vector<int> system_load_samplings_;
  util::ProcStat proc_stat_;
  int process_average_load_;
  int process_load_sampling_index_;
  std::vector<int> process_load_samplings_;

  int FindAccurateBaseLoopCount(int max_iteration);
  void UpdateProcStat(TimePoint time_point);
  void UpdateSystemSamplingVector(int current);
  void CalculateSystemAverageLoad();
  void UpdateProcessSamplingVector(int current);
  void CalculateProcessAverageLoad();
};

}  // namespace cpu
