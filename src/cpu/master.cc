#include "master.h"

#include "cpu/cpu.h"
#include "options.h"
#include "util/log.h"

namespace cpu {

CpuResourceManager::CpuResourceManager(const Options &options)
    : ResourceManager(options), jiffy_ms_(cpu::GetJiffyMillisecond()) {}

bool CpuResourceManager::Init() {
  int count;
  if (options_.cpu_count_ > 0) {
    count = options_.cpu_count_;
  } else {
    count = (options_.cpu_load_ + (kMaxLoadPerCore - 1)) / kMaxLoadPerCore;
  }
  if (count > Count()) {
    LOG_E("CPU load `%d` needs %d CPU, have %d", options_.cpu_load_, count, Count());
    return false;
  }
  if (count <= 0) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    CpuWorkerContext ctx(i, wg_);
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
    auto refresh_ret = GetProcStat(stat_info_);
    if (refresh_ret != ErrCode::kOK) {
      LOG_E("failed to GetProcStat, ret=%d", ErrCodeToInt(refresh_ret));
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
    auto cpu_load = static_cast<int>(static_cast<double>(cpu_ms) / elapsed_ms * 100.0);
    // LOG("cpu_load=%d", cpu_load);
    this->AdjustWorkerLoad(cpu_load);
  }

  time_point_ = time_point;
}

CpuResourceManagerSimple::CpuResourceManagerSimple(const Options &options)
    : CpuResourceManager(options) {}

void CpuResourceManagerSimple::AdjustWorkerLoad(int cpu_load) {
  int core_target = 0;
  int last_wasted_load = 0;
  for (const auto &ctx : workers_) {
    last_wasted_load += ctx.load_set_;
  }

  do {
    // set to 0 if heavy loaded now
    if (cpu_load > kPauseLoopCpuThreshold * Count()) {
      core_target = 0;
      break;
    }
    // maximum idle logic
    auto idle_load = (cpu::Count() * kMaxLoadPerCore) - cpu_load - last_wasted_load;
    if (idle_load > options_.cpu_load_) {
      idle_load = options_.cpu_load_;
    }
    core_target = idle_load / static_cast<int>(workers_.size());
  } while (false);

  for (auto &ctx : workers_) {
    ctx.load_set_ = core_target;
  }
}

}  // namespace cpu
