#pragma once

#include <cstdint>

#include <chrono>

namespace util {

class ProcStat final {
 public:
  using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

  enum class ForceUpdate { kNo, kYes };

  explicit ProcStat(int pid);
  void UpdateCpuStat(TimePoint now, ForceUpdate force = ForceUpdate::kNo);
  int GetCpuLoad() const { return cpu_load_cached_; }  // 100 each core
  uint64_t GetMemory() const;                          // in bytes

 private:
  int pid_;
  TimePoint time_point_;
  uint64_t jiffies_self_;
  int64_t jiffies_child_;
  int cpu_load_cached_;
};

}  // namespace util
