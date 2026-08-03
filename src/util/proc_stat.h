#pragma once

#include <cstdint>

#include <chrono>
#include <optional>

#include "core/platform.h"

namespace util {

// Tracks this process's own CPU usage and exposes it as a 0..100-per-core
// load figure. The per-platform backends (proc_stat_<platform>.cc) read the
// raw cumulative counter in its NATIVE unit (Linux: jiffies, BSD:
// microseconds, Windows: 100ns ticks, macOS: nanoseconds -- the kernel
// gives ns directly); the platform-agnostic core diffs two readings in
// tick space and only converts the small DIFF to nanoseconds. This keeps
// the cumulative counter off the nanosecond path, where it would overflow
// uint64 after ~9 years on a process pegging 64 cores (Linux/BSD/Windows
// native units do not wrap in realistic uptime). macOS's native unit IS
// nanoseconds, so its cumulative counter is still bounded by the kernel's
// 64-bit ns width -- an inherent limit, not fixable at this layer.
class ProcStat final {
 public:
  using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

  enum class ForceUpdate { kNo, kYes };

  explicit ProcStat();
  void UpdateCpuStat(TimePoint now, ForceUpdate force = ForceUpdate::kNo);
  int GetCpuLoad() const { return cpu_load_cached_; }  // 100 each core

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
  TimePoint time_point_;
  // Cumulative CPU time consumed by this process, in native ticks, captured
  // at the previous update. Diffs are taken in tick space (never
  // overflowing) and only the small diff is converted to ns via
  // ProcessCpuTicksToNs().
  uint64_t prev_cpu_ticks_ = 0;
  int cpu_load_cached_ = 0;
};

// Convert a process-CPU tick DIFF to nanoseconds. Native tick -> ns is
// platform-specific (Linux: diff*1e9/HZ, BSD: *1000, Windows: *100, macOS:
// identity, ns already). Only safe for diffs, never cumulative values.
uint64_t ProcessCpuTicksToNs(uint64_t tick_diff);

}  // namespace util
