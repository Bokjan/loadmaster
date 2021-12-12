#include "manager.h"

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
      LOG_ERROR("failed to GetProcStat, ret=%d", ErrCodeToInt(refresh_ret));
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

}  // namespace cpu
