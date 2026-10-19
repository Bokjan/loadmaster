#pragma once

#include <memory>
#include <vector>

#include "constants.h"
#include "load_controller.h"
#include "stat.h"
#include "worker.h"

#include "core/resource_manager.h"
#include "util/proc_stat.h"
#include "util/rolling_sampler.h"

namespace cpu {

class CpuResourceManager : public core::ResourceManager {
 public:
  void CreateWorkerThreads() override final;
  void RequestWorkerThreadsStop() override final;
  void JoinWorkerThreads() override final;
  void Schedule(TimePoint time_point) override final;

 protected:
  explicit CpuResourceManager(const core::Options &options);
  virtual void AdjustWorkerLoad(TimePoint time_point, int system_load) = 0;

  bool ConstructWorkerThreads(int count);
  // Split `total_load` (percent-of-a-core units) across the workers as
  // evenly as possible; the remainder goes one unit each to the first
  // workers rather than being dropped.
  void SetWorkerLoadWithTotalLoad(int total_load);
  // Total load the workers should burn this tick to bring the system-wide
  // average to `target`. See cpu::LoadController.
  int CalculateLoadDemand(int target);
  // The most the workers can burn: one full core each.
  int Capacity() const { return static_cast<int>(workers_.size()) * kCpuMaxLoadPerCore; }

  double GetSystemAverageLoad() const { return system_sampler_.GetMean(); }
  double GetProcessAverageLoad() const { return process_sampler_.GetMean(); }

  TimePoint GetLastScheduling() const { return last_scheduling_; }
  void SetLastScheduling(TimePoint tp) { last_scheduling_ = tp; }

 private:
  // unique_ptr because CpuWorkerContext owns non-movable members
  // (std::atomic, std::jthread).
  std::vector<std::unique_ptr<CpuWorkerContext>> workers_;
  int base_loop_count_;
  // System-wide cumulative busy time (ns) captured at the previous
  // Schedule() tick. 0 means "no prior sample yet".
  uint64_t prev_system_busy_ns_ = 0;
  util::RollingSampler<double> system_sampler_;
  util::ProcStat proc_stat_;
  util::RollingSampler<double> process_sampler_;
  LoadController controller_;
  TimePoint last_scheduling_;

  int FindAccurateBaseLoopCount(int max_iteration);
  void UpdateProcStat(TimePoint time_point);
};

}  // namespace cpu
