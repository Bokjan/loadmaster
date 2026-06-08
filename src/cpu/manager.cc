#include "manager.h"

#include <algorithm>
#include <numeric>

#include "constants.h"
#include "critical_loop.h"
#include "stat.h"

#include "util/log.h"

namespace cpu {

CpuResourceManager::CpuResourceManager(const core::Options &options)
    : ResourceManager(options),
      base_loop_count_(0),
      system_sampler_(kCpuAvgLoadSamplingCount),
      proc_stat_(),
      process_sampler_(kCpuAvgLoadSamplingCount) {
  // Find a finest base loop count
  base_loop_count_ = FindAccurateBaseLoopCount(kCpuBaseLoopCountTestIteration);
}

void CpuResourceManager::CreateWorkerThreads() {
  for (auto &ctx : workers_) {
    ctx->Start();
  }
}

void CpuResourceManager::RequestWorkerThreadsStop() {
  for (auto &ctx : workers_) {
    ctx->RequestStop();
  }
}

void CpuResourceManager::JoinWorkerThreads() {
  for (auto &ctx : workers_) {
    ctx->Join();
  }
}

void CpuResourceManager::Schedule(TimePoint time_point) {
  // Refresh system CPU snapshot (cumulative busy nanoseconds).
  const std::optional<uint64_t> current_busy_ns = ReadSystemBusyNs();
  if (!current_busy_ns) {
    LOG_ERROR("failed to ReadSystemBusyNs");
    SetLastScheduling(time_point);
    return;
  }

  // First call: record the baseline and bail -- nothing to diff against yet.
  // Unlike the old snapshot-member approach this MUST be stored explicitly,
  // otherwise the next tick would diff against 0 and spike once.
  if (prev_system_busy_ns_ == 0) {
    prev_system_busy_ns_ = *current_busy_ns;
    SetLastScheduling(time_point);
    return;
  }

  // Update process snapshot/average.
  UpdateProcStat(time_point);

  // Compute current system-wide CPU load (platform-agnostic, ns-based).
  // Guard against a non-monotonic reading so a counter glitch can't
  // underflow the unsigned diff.
  const uint64_t busy_ns_diff =
      (*current_busy_ns >= prev_system_busy_ns_) ? (*current_busy_ns - prev_system_busy_ns_) : 0;
  const auto elapsed_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(time_point - GetLastScheduling())
          .count();
  if (elapsed_ns <= 0) {
    prev_system_busy_ns_ = *current_busy_ns;
    SetLastScheduling(time_point);
    return;
  }
  // load = busy_ns / elapsed_ns * 100 (per-core units, summed across cores).
  const int system_load = static_cast<int>(static_cast<double>(busy_ns_diff) /
                                           static_cast<double>(elapsed_ns) * kCpuMaxLoadPerCore);
  system_sampler_.InsertValue(system_load);
  LOG_TRACE("cur_sys_load=%d, avg_sys_load=%d", system_load, system_sampler_.GetMean());

  // Invoke specified scheduler.
  AdjustWorkerLoad(time_point, system_load);

  prev_system_busy_ns_ = *current_busy_ns;
  SetLastScheduling(time_point);
}

int CpuResourceManager::FindAccurateBaseLoopCount(int max_iteration) {
  int min = kCpuBaseLoopCountMin;
  int max = kCpuBaseLoopCountMax;
  int iteration = 0;
  int base_loop_count = 0;
  int accurate_loop_count = 0;
  int64_t accurate_elapsed = 0;

  do {
    int new_base_loop_count = std::midpoint(min, max);
    if (new_base_loop_count == base_loop_count) {
      break;
    }
    base_loop_count = new_base_loop_count;
    auto start = std::chrono::high_resolution_clock::now();
    cpu::CriticalLoop(base_loop_count);
    auto stop = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    if (elapsed > kCpuSchedulingGranularityNS) {
      max = base_loop_count;
    } else {
      min = base_loop_count;
    }
    LOG_TRACE("iteration=%d, diff=%ld, min=%d, max=%d, mid=%d", iteration, elapsed, min, max,
              base_loop_count);
    if (std::abs(elapsed - kCpuSchedulingGranularityNS) <
        std::abs(accurate_elapsed - kCpuSchedulingGranularityNS)) {
      accurate_elapsed = elapsed;
      accurate_loop_count = base_loop_count;
    }
    ++iteration;
  } while (iteration < max_iteration);

  // Ensure we never return 0 (would make CriticalLoop a no-op and cause UB
  // in some downstream math). Workers will further refine this at runtime.
  if (accurate_loop_count <= 0) {
    accurate_loop_count = 1;
  }
  return accurate_loop_count;
}

void CpuResourceManager::UpdateProcStat(TimePoint time_point) {
  proc_stat_.UpdateCpuStat(time_point);
  process_sampler_.InsertValue(proc_stat_.GetCpuLoad());
}

bool CpuResourceManager::ConstructWorkerThreads(int count) {
  workers_.reserve(count);
  for (int i = 0; i < count; ++i) {
    workers_.push_back(std::make_unique<CpuWorkerContext>(i, base_loop_count_));
  }
  return true;
}

void CpuResourceManager::SetWorkerLoadWithTotalLoad(int total_load) {
  const int thread_count = static_cast<int>(workers_.size());
  if (thread_count == 0) {
    return;
  }
  const int avg_load = total_load / thread_count;
  for (auto &th : workers_) {
    th->SetLoadSet(avg_load);
  }
}

int CpuResourceManager::CalculateLoadDemand(int target) {
  // Equation: target = other + proc, other = sysavg - procavg
  // Then, we assume C = sampling count,  K = C + 1
  // We have: target * K = other * K + [procavg * C + demand]
  //          K * (sysavg - procavg) + [procavg * C + demand] = K * target
  // That is: demand = K * (target - other) - C * procavg
  const int sysavg = system_sampler_.GetMean();
  const int procavg = process_sampler_.GetMean();
  const int other = sysavg - procavg;
  const int C = system_sampler_.GetSampleCount();
  const int K = C + 1;
  return K * (target - other) - C * procavg;
}

}  // namespace cpu
