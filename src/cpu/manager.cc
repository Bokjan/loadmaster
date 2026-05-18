#include "manager.h"

#include <algorithm>
#include <numeric>

#include "critical_loop.h"
#include "stat.h"

#include "core/constants.h"
#include "util/log.h"
#include "util/win_util.h"

#if !IS_WINDOWS
#  include <unistd.h>
#endif

namespace cpu {

namespace {

#if !IS_WINDOWS
inline uint64_t GetConcernedJiffies(const CpuStatInfo &info) {
  return info.user + info.nice + info.system;
}
inline uint64_t GetMilliFromEpoch(uint64_t epoch) {
  static auto jiffy_ms = GetJiffyMillisecond();
  return epoch * jiffy_ms;
}
#else
inline uint64_t GetConcernedJiffies(const CpuStatInfo &info) {
  return info.windows_concerned_100ns_;
}
inline uint64_t GetMilliFromEpoch(uint64_t epoch) {
  return util::WindowsEpochToMilliseconds(epoch);
}
#endif

}  // namespace

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
    ctx.jthread_ = std::jthread([&](std::stop_token stoken) { ctx.Loop(stoken); });
  }
}

void CpuResourceManager::RequestWorkerThreadsStop() {
  for (auto &ctx : workers_) {
    ctx.jthread_.request_stop();
  }
}

void CpuResourceManager::JoinWorkerThreads() {
  for (auto &ctx : workers_) {
    if (ctx.jthread_.joinable()) {
      ctx.jthread_.join();
    }
  }
}

void CpuResourceManager::Schedule(TimePoint time_point) {
  const auto last_jiffies = GetConcernedJiffies(cpu_stat_);

  // Refresh system CPU snapshot.
  try {
    if (!GetCpuProcStat(cpu_stat_)) {
      LOG_ERROR("failed to GetCpuProcStat");
      SetLastScheduling(time_point);
      return;
    }
  } catch (const std::exception &e) {
    LOG_ERROR("GetCpuProcStat threw: %s", e.what());
    SetLastScheduling(time_point);
    return;
  }

  // First call: nothing to diff against yet.
  if (last_jiffies == 0) {
    SetLastScheduling(time_point);
    return;
  }

  // Update process snapshot/average.
  UpdateProcStat(time_point);

  // Compute current system-wide CPU load.
  const auto current_jiffies = GetConcernedJiffies(cpu_stat_);
  const auto diff = current_jiffies - last_jiffies;
  const auto cpu_ms = GetMilliFromEpoch(diff);
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(time_point - GetLastScheduling())
          .count();
  if (elapsed_ms <= 0) {
    SetLastScheduling(time_point);
    return;
  }
  const int system_load =
      static_cast<int>(static_cast<double>(cpu_ms) / elapsed_ms * kCpuMaxLoadPerCore);
  system_sampler_.InsertValue(system_load);
  LOG_TRACE("cur_sys_load=%d, avg_sys_load=%d", system_load, system_sampler_.GetMean());

  // Invoke specified scheduler.
  AdjustWorkerLoad(time_point, system_load);

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
  // in some downstream math).
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
    workers_.emplace_back(i, base_loop_count_);
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
    th.SetLoadSet(avg_load);
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
