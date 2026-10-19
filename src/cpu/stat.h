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

// Read the system-wide cumulative "busy" CPU time, in platform-native
// ticks, summed across all cores and accumulated since boot. The value is
// monotonically non-decreasing between successive calls on the same
// machine; callers diff two readings to derive a load over an interval.
//
// Native ticks (Linux/macOS jiffies, BSD statclock ticks, Windows 100ns
// ticks) are coarse enough that the cumulative counter does not wrap in
// any realistic uptime, so the diff is taken in tick space and only the
// small diff is converted to nanoseconds via BusyTicksToNs(). A cumulative
// *nanosecond* count overflows uint64_t after ~584 core-years of busy time
// (~9 years of uptime on 64 cores) and would then read as zero load.
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
std::optional<uint64_t> ReadSystemBusyTicks();

// Convert a native tick DIFF to nanoseconds. Safe only for diffs (one
// scheduling interval, or a stalled gap of minutes at most) -- never for
// cumulative values, whose conversion is exactly what overflows. On tick
// platforms this is util::TicksToNs(diff, hz); on Windows `diff * 100`.
// Pure: tick diff in, ns out.
uint64_t BusyTicksToNs(uint64_t tick_diff);

}  // namespace cpu
