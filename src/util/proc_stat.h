#pragma once

#include <cstdint>

#include <chrono>
#include <optional>

#include "core/platform.h"

namespace util {

// Convert a DIFF of the counter ReadProcessCpuTicks() returns to
// nanoseconds. Implemented per-platform next to the reader. Only for diffs:
// converting the cumulative value is what would overflow (see below).
uint64_t ProcessCpuTicksToNs(uint64_t tick_diff);

// Tracks this process's own CPU usage and exposes it as a 0..100-per-core
// load figure. The per-platform backends (proc_stat_<platform>.cc) read
// the CPU-time counter in its native unit (Linux jiffies, BSD
// microseconds, Windows 100ns ticks, macOS Mach absolute-time units); the
// diff between two readings is taken in that unit and only the diff is
// converted to nanoseconds. A cumulative nanosecond count would overflow
// uint64_t after ~584 core-years of CPU time (~9 years for a process
// keeping 64 cores busy) and then read as zero load.
class ProcStat final {
 public:
  using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

  enum class ForceUpdate { kNo, kYes };

  explicit ProcStat();
  void UpdateCpuStat(TimePoint now, ForceUpdate force = ForceUpdate::kNo);
  double GetCpuLoad() const { return cpu_load_cached_; }  // 100 each core

 private:
  // Read this process's cumulative CPU time in platform-native ticks.
  // Implemented per-platform in proc_stat_<platform>.cc. Returns nullopt on
  // transient failure (the caller then keeps the previous cached load).
  std::optional<uint64_t> ReadProcessCpuTicks() const;

  // The process identity differs by platform: a pid on POSIX, a HANDLE on
  // Windows. This is a genuine platform difference (not a unit hack), so a
  // minimal #if here is justified.
#if IS_WINDOWS
  HANDLE process_handle_ = nullptr;
#else
  int pid_ = 0;
#endif
  // Monotonic: a wall-clock step (NTP, VM resume, `date`) must not freeze
  // or spike the computed load.
  TimePoint time_point_;
  bool has_baseline_ = false;
  // Cumulative CPU time consumed by this process, in native ticks, captured
  // at the previous update.
  uint64_t prev_cpu_ticks_ = 0;
  double cpu_load_cached_ = 0.0;
};

}  // namespace util
