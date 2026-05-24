#pragma once

#include <cstdint>

#include <thread>

#include "core/platform.h"

namespace cpu {

inline int CoreCount() { return static_cast<int>(std::thread::hardware_concurrency()); }

// Snapshot of system-wide CPU usage; layout is platform-specific. Use the
// helpers below to extract platform-agnostic values.
struct CpuStatInfo {
#if !IS_WINDOWS
  uint64_t user = 0;
  uint64_t nice = 0;
  uint64_t system = 0;
  uint64_t idle = 0;
  uint64_t iowait = 0;
  uint64_t irq = 0;
  uint64_t softirq = 0;
  uint64_t steal = 0;
  uint64_t guest = 0;
  uint64_t guest_nice = 0;
#else
  uint64_t windows_concerned_100ns = 0;
#endif
};

// Refresh `info` from /proc/stat (Linux) or GetSystemTimes (Windows).
// Returns true on success; throws std::runtime_error on hard failure
// (e.g. /proc/stat unreadable).
bool GetCpuProcStat(CpuStatInfo &info);

// Total "busy" time accumulated in `info`, expressed in a platform-defined
// unit (jiffies on Linux, 100-ns ticks on Windows).
uint64_t GetBusyTicks(const CpuStatInfo &info);

// Convert the unit returned by GetBusyTicks() to milliseconds.
uint64_t TicksToMilliseconds(uint64_t ticks);

}  // namespace cpu
