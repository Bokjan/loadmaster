#include "manager.h"

#include "cpu/cpu.h"
#include "options.h"
#include "util/log.h"

#include <unistd.h>

namespace cpu {

CpuResourceManager::CpuResourceManager(const Options &options)
    : ResourceManager(options),
      jiffy_ms_(cpu::GetJiffyMillisecond()),
      base_loop_count_(0),
      system_average_load_(0),
      system_load_sampling_index_(0),
      proc_stat_(getpid()),
      process_average_load_(0),
      process_load_sampling_index_(0) {
  // Find a finest base loop count
  base_loop_count_ = FindAccurateBaseLoopCount(kCpuBaseLoopCountTestIteration);
  // Allocate memory for load sampling vector
  system_load_samplings_.reserve(kCpuAvgLoadSamplingCount);
  process_load_samplings_.reserve(kCpuAvgLoadSamplingCount);
}

void CpuResourceManager::CreateWorkerThreads() {
  for (auto &ctx : workers_) {
    ctx.thread_ = std::thread([&]() { ctx.Loop(); });
    wg_.Incr();
    ctx.thread_.detach();
  }
}

inline uint64_t GetUserNiceSystemJiffies(const CpuStatInfo &info) {
  return info.user + info.nice + info.system;
}

void CpuResourceManager::Schedule(TimePoint time_point) {
  bool will_schedule = false;
  auto last_jiffies = GetUserNiceSystemJiffies(cpu_stat_);

  do {
    // refresh `cpu_stat_`
    if (!GetCpuProcStat(cpu_stat_)) {
      LOG_FATAL("failed to GetCpuProcStat");
      break;
    }
    if (last_jiffies == 0) {
      break;
    }
    will_schedule = true;
  } while (false);

  if (will_schedule) {
    // Update average load
    this->UpdateProcStat(time_point);
    // Calculate system load
    auto current_jiffies = GetUserNiceSystemJiffies(cpu_stat_);
    auto diff = current_jiffies - last_jiffies;
    auto cpu_ms = diff * jiffy_ms_;
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(time_point - last_scheduling_)
            .count();
    auto system_load =
        static_cast<int>(static_cast<double>(cpu_ms) / elapsed_ms * kCpuMaxLoadPerCore);
    this->UpdateSystemSamplingVector(system_load);
    this->CalculateSystemAverageLoad();
    LOG_TRACE("cur_sys_load=%d, avg_sys_load=%d", system_load, system_average_load_);
    // Invoke specified scheduler
    this->AdjustWorkerLoad(time_point, system_load);
  }

  last_scheduling_ = time_point;
}

int CpuResourceManager::FindAccurateBaseLoopCount(int max_iteration) {
  int min = kCpuBaseLoopCountMin;
  int max = kCpuBaseLoopCountMax;
  int iteration = 0;
  int base_loop_count = 0;
  int accurate_loop_count = 0;
  int64_t accurate_elapsed = 0;

  do {
    int new_base_loop_count = (min + max) / 2;
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
    LOG_TRACE("iteration=%d, diff=%ld, min=%d, max=%d, mid=%d, start=%ld, stop=%ld", iteration,
              elapsed, min, max, base_loop_count, start.time_since_epoch().count(),
              stop.time_since_epoch().count());
    if (std::abs(elapsed - kCpuSchedulingGranularityNS) <
        std::abs(accurate_elapsed - kCpuSchedulingGranularityNS)) {
      accurate_elapsed = elapsed;
      accurate_loop_count = base_loop_count;
      LOG_TRACE("iteration=%d, acc_elapsed=%ld, acc_loop_count=%d", iteration, accurate_elapsed,
                accurate_loop_count);
    }
    ++iteration;
  } while (iteration < max_iteration);

  return accurate_loop_count;
}

void CpuResourceManager::UpdateProcStat(TimePoint time_point) {
  proc_stat_.UpdateCpuStat(time_point);
  this->UpdateProcessSamplingVector(proc_stat_.GetCpuLoad());
  this->CalculateProcessAverageLoad();
}

bool CpuResourceManager::ConstructWorkerThreads(int count) {
  for (int i = 0; i < count; ++i) {
    CpuWorkerContext ctx(i, wg_, base_loop_count_);
    workers_.emplace_back(std::move(ctx));
  }
  return true;
}

void CpuResourceManager::UpdateProcessSamplingVector(int current) {
  if (process_load_samplings_.size() < kCpuAvgLoadSamplingCount) {
    process_load_samplings_.emplace_back(current);
  } else {
    process_load_samplings_[process_load_sampling_index_] = current;
    ++process_load_sampling_index_;
    if (process_load_sampling_index_ >= kCpuAvgLoadSamplingCount) {
      process_load_sampling_index_ = 0;
    }
  }
}

void CpuResourceManager::CalculateProcessAverageLoad() {
  if (process_load_samplings_.empty()) {
    process_average_load_ = 0;
    return;
  }
  int total = 0;
  for (auto item : process_load_samplings_) {
    total += item;
  }
  process_average_load_ = total / process_load_samplings_.size();
}

void CpuResourceManager::SetWorkerLoadWithTotalLoad(int total_load) {
  int thread_count = workers_.size();
  int avg_load = total_load / thread_count;
  for (auto &th : workers_) {
    th.SetLoadSet(avg_load);
  }
}

void CpuResourceManager::UpdateSystemSamplingVector(int current) {
  if (system_load_samplings_.size() < kCpuAvgLoadSamplingCount) {
    system_load_samplings_.emplace_back(current);
  } else {
    system_load_samplings_[system_load_sampling_index_] = current;
    ++system_load_sampling_index_;
    if (system_load_sampling_index_ >= kCpuAvgLoadSamplingCount) {
      system_load_sampling_index_ = 0;
    }
  }
}

void CpuResourceManager::CalculateSystemAverageLoad() {
  if (system_load_samplings_.empty()) {
    system_average_load_ = 0;
    return;
  }
  int total = 0;
  for (auto item : system_load_samplings_) {
    total += item;
  }
  system_average_load_ = total / system_load_samplings_.size();
}

int CpuResourceManager::CalculateLoadDemand(int target) {
  // Equation: target = other + proc, other = sysavg - procavg
  // Then, we assume C = sampling count,  K = C + 1
  // We have: target * K = other * K + [procavg * C + demand]
  //          K * (sysavg - procavg) + [procavg * C + demand] = K * target
  // That is: demand = K * (target - other) - C * procavg
  const int sysavg = GetSystemAverageLoad();
  const int procavg = GetProcessAverageLoad();
  const int other = sysavg - procavg;
  const int C = system_load_samplings_.size();
  const int K = C + 1;
  int demand = K * (target - other) - C * procavg;
  return demand;
}

}  // namespace cpu
