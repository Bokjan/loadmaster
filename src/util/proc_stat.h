#pragma once

#include <cstdint>

#include <chrono>

#include "core/constants.h"

namespace util {

class ProcStat final {
 public:
  using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

  enum class ForceUpdate { kNo, kYes };

  explicit ProcStat();
  void UpdateCpuStat(TimePoint now, ForceUpdate force = ForceUpdate::kNo);
  int GetCpuLoad() const { return cpu_load_cached_; }  // 100 each core
  uint64_t GetMemory() const;                          // in bytes

 private:
#if !IS_WINDOWS
  int pid_;
#else
  HANDLE process_handle_ = nullptr;
#endif
  TimePoint time_point_;
#if !IS_WINDOWS
  uint64_t jiffies_self_ = 0;
  int64_t jiffies_child_ = 0;
#else
  uint64_t epoch_ = 0; // 100 ns
#endif
  int cpu_load_cached_ = 0;
};

}  // namespace util
