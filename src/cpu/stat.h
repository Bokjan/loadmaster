#pragma once

#include <cstdint>
#include <optional>

namespace cpu {

// Number of CPUs this process may actually run on. Starts from
// std::thread::hardware_concurrency() and, on Linux, narrows it to the
// sched_getaffinity() set (taskset / cpuset containers) and to a cgroup v2
// `cpu.max` quota rounded up (docker --cpus). Cached after the first call;
// always >= 1.
int CoreCount();

// Read the system-wide cumulative "busy" CPU time, in nanoseconds, summed
// across all cores and accumulated since boot. The value is monotonically
// non-decreasing between successive calls on the same machine; callers diff
// two readings to derive a load over an interval.
//
// "Busy" means "not idle". Each platform converges on that single intent --
// everything the kernel was NOT idle for counts as busy -- even though the
// native counters differ:
//   * Linux:   user + nice + system + irq + softirq + steal
//              (idle and iowait are deliberately EXCLUDED -> not busy),
//              summed over the CPUs in our affinity set only, so a
//              taskset / cpuset-restricted run measures the CPUs it can
//              actually load rather than the whole host
//   * macOS:   user + nice + system   (Mach folds IRQ time into system)
//   * Windows: user + (kernel - idle) (kernel time on Windows INCLUDES idle,
//                                      so idle must be subtracted back out)
//
// Returns std::nullopt on hard failure (e.g. /proc/stat unreadable,
// host_statistics64 / GetSystemTimes failed). Never throws.
std::optional<uint64_t> ReadSystemBusyNs();

}  // namespace cpu
