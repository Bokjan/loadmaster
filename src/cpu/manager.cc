#include "manager.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "constants.h"
#include "critical_loop.h"
#include "stat.h"

#include "core/constants.h"
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
  // Refresh system CPU snapshot (cumulative busy time, native ticks).
  const std::optional<uint64_t> current_busy_ticks = ReadSystemBusyTicks();
  if (!current_busy_ticks) {
    // Keep both the busy baseline and its timestamp, so the next
    // successful read diffs N intervals of busy time against N intervals
    // of elapsed time. (Advancing only the timestamp used to read ~2x the
    // real load on the following tick.)
    LOG_ERROR("failed to ReadSystemBusyTicks");
    return;
  }

  // First call: record the baseline and bail -- nothing to diff against yet.
  if (!has_prev_busy_ticks_) {
    prev_busy_ticks_ = *current_busy_ticks;
    has_prev_busy_ticks_ = true;
    SetLastScheduling(time_point);
    return;
  }

  // Update process snapshot/average.
  UpdateProcStat(time_point);

  // Compute current system-wide CPU load. The diff is taken in tick space
  // and only then converted to ns (see cpu/stat.h). Guard against a
  // non-monotonic reading so a counter glitch can't underflow the unsigned
  // diff.
  const uint64_t busy_ticks_diff =
      (*current_busy_ticks >= prev_busy_ticks_) ? (*current_busy_ticks - prev_busy_ticks_) : 0;
  const auto elapsed_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(time_point - GetLastScheduling())
          .count();
  if (elapsed_ns <= 0) {
    prev_busy_ticks_ = *current_busy_ticks;
    SetLastScheduling(time_point);
    return;
  }
  // load = busy_ns / elapsed_ns * 100 (per-core units, summed across cores).
  const double system_load_exact = static_cast<double>(BusyTicksToNs(busy_ticks_diff)) /
                                   static_cast<double>(elapsed_ns) * kCpuMaxLoadPerCore;
  system_sampler_.InsertValue(system_load_exact);
  const int system_load = static_cast<int>(std::lround(system_load_exact));
  LOG_TRACE("cur_sys_load=%d, avg_sys_load=%.1f", system_load, system_sampler_.GetMean());

  // Invoke specified scheduler.
  AdjustWorkerLoad(time_point, system_load);

  prev_busy_ticks_ = *current_busy_ticks;
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
    auto start = std::chrono::steady_clock::now();
    cpu::CriticalLoop(base_loop_count);
    auto stop = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    if (elapsed > kCpuSchedulingGranularityNS) {
      max = base_loop_count;
    } else {
      min = base_loop_count;
    }
    LOG_TRACE("iteration=%d, diff=%lld, min=%d, max=%d, mid=%d", iteration,
              static_cast<long long>(elapsed), min, max,
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
  total_load = std::clamp(total_load, 0, Capacity());
  const int base = total_load / thread_count;
  const int remainder = total_load % thread_count;
  for (int i = 0; i < thread_count; ++i) {
    const int load_percent = base + (i < remainder ? 1 : 0);
    // Load is "percent of one core"; the worker wants busy milliseconds
    // per scheduling period. (Identical while kScheduleIntervalMS == 100,
    // but no longer silently tied to that value.)
    workers_[i]->SetLoadSet(load_percent * kScheduleIntervalMS / kCpuMaxLoadPerCore);
  }
}

int CpuResourceManager::CalculateLoadDemand(int target) {
  return controller_.Update(target, system_sampler_.GetMean(), process_sampler_.GetMean(),
                            Capacity());
}

}  // namespace cpu
