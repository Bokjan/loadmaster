#include "manager.h"

#include "cpu/cpu.h"
#include "options.h"
#include "util/log.h"

namespace cpu {

CpuResourceManager::CpuResourceManager(const Options &options)
    : ResourceManager(options), jiffy_ms_(cpu::GetJiffyMillisecond()), base_loop_count_(0) {
  // Find a finest base loop count
  base_loop_count_ = FindAccurateBaseLoopCount(kCpuBaseLoopCountTestIteration);
}

bool CpuResourceManager::Init() {
  int count;
  if (options_.cpu_count_ > 0) {
    count = options_.cpu_count_;
  } else {
    count = (options_.cpu_load_ + (kCpuMaxLoadPerCore - 1)) / kCpuMaxLoadPerCore;
  }
  if (count > Count()) {
    LOG_ERROR("CPU load `%d` needs %d CPU, have %d", options_.cpu_load_, count, Count());
    return false;
  }
  if (count <= 0) {
    return false;
  }

  for (int i = 0; i < count; ++i) {
    CpuWorkerContext ctx(i, wg_, base_loop_count_);
    workers_.emplace_back(std::move(ctx));
  }
  return true;
}

void CpuResourceManager::CreateWorkerThreads() {
  for (auto &ctx : workers_) {
    ctx.thread_ = std::thread([&]() { ctx.Loop(); });
    wg_.Incr();
    ctx.thread_.detach();
  }
}

inline uint64_t GetUserNiceSystemJiffies(const StatInfo &info) {
  return info.user + info.nice + info.system;
}

void CpuResourceManager::Schedule(TimePoint time_point) {
  bool will_schedule = false;
  auto last_jiffies = GetUserNiceSystemJiffies(stat_info_);

  do {
    // refresh `stat_info_`
    if (!GetProcStat(stat_info_)) {
      LOG_FATAL("failed to GetProcStat");
      break;
    }
    if (last_jiffies == 0) {
      break;
    }
    will_schedule = true;
  } while (false);

  if (will_schedule) {
    auto current_jiffies = GetUserNiceSystemJiffies(stat_info_);
    auto diff = current_jiffies - last_jiffies;
    auto cpu_ms = diff * jiffy_ms_;
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(time_point - time_point_).count();
    auto cpu_load = static_cast<int>(static_cast<double>(cpu_ms) / elapsed_ms * kCpuMaxLoadPerCore);
    LOG_TRACE("cpu_load=%d", cpu_load);
    this->AdjustWorkerLoad(time_point, cpu_load);
  }

  time_point_ = time_point;
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

}  // namespace cpu
