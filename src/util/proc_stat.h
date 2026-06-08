#pragma once

#include <cstdint>

#include <chrono>
#include <optional>

#include "core/platform.h"

namespace util {

// Tracks this process's own CPU usage and exposes it as a 0..100-per-core
// load figure. All platforms normalize their native CPU-time counters to
// nanoseconds before storing them, so there is a single unit throughout --
// the per-platform backends (proc_stat_<platform>.cc) only differ in how
// they read the raw counter.
class ProcStat final {
 public:
  using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

  enum class ForceUpdate { kNo, kYes };

  explicit ProcStat();
  void UpdateCpuStat(TimePoint now, ForceUpdate force = ForceUpdate::kNo);
  int GetCpuLoad() const { return cpu_load_cached_; }  // 100 each core

 private:
  // Read this process's cumulative CPU time in nanoseconds. Implemented
  // per-platform in proc_stat_<platform>.cc. Returns nullopt on transient
  // failure (the caller then keeps the previous cached load).
  std::optional<uint64_t> ReadProcessCpuNs() const;

  // The process identity differs by platform: a pid on POSIX, a HANDLE on
  // Windows. This is a genuine platform difference (not a unit hack), so a
  // minimal #if here is justified.
#if IS_WINDOWS
  HANDLE process_handle_ = nullptr;
#else
  int pid_ = 0;
#endif
  TimePoint time_point_;
  // Cumulative CPU time consumed by this process, in nanoseconds, captured
  // at the previous update. Unit is identical on every platform now --
  // replaces the old jiffies_self_ / jiffies_child_ / epoch_ members that
  // each carried a different (and on macOS, mislabeled) unit.
  uint64_t prev_cpu_ns_ = 0;
  int cpu_load_cached_ = 0;
};

}  // namespace util
