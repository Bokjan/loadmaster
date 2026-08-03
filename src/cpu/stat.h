#pragma once

#include <cstdint>
#include <optional>
#include <thread>

namespace cpu {

inline int CoreCount() { return static_cast<int>(std::thread::hardware_concurrency()); }

// Read the system-wide cumulative "busy" CPU time, in platform-native
// ticks, summed across all cores and accumulated since boot. The value is
// monotonically non-decreasing between successive calls on the same
// machine; callers diff two readings to derive a load over an interval.
//
// Native ticks (Linux/macOS/BSD jiffies, Windows 100ns ticks) are small
// enough that the cumulative counter does not wrap in any realistic
// uptime, so the diff is taken in tick space (never overflowing) and only
// the small diff is converted to nanoseconds via BusyTicksToNs(). The
// earlier ReadSystemBusyNs() returned cumulative *nanoseconds*, which on
// jiffy platforms overflowed uint64 after ~9 years (64 cores) / ~35 years
// (16 cores) of uptime and forced a wrap-to-zero misread of the control
// law; diffing in tick space removes that horizon entirely.
//
// "Busy" means "not idle". Each platform converges on that single intent --
// everything the kernel was NOT idle for counts as busy -- even though the
// native counters differ:
//   * Linux:   user + nice + system + irq + softirq + steal
//              (idle and iowait are deliberately EXCLUDED -> not busy)
//   * macOS:   user + nice + system   (Mach folds IRQ time into system)
//   * Windows: user + (kernel - idle) (kernel time on Windows INCLUDES idle,
//                                      so idle must be subtracted back out)
//
// Returns std::nullopt on hard failure (e.g. /proc/stat unreadable,
// host_statistics64 / GetSystemTimes failed). Never throws.
std::optional<uint64_t> ReadSystemBusyTicks();

// Convert a native tick DIFF to nanoseconds. Safe only for diffs (one
// scheduling interval, or a stalled gap of minutes at most) -- never for
// cumulative values, whose conversion is exactly what overflows. On jiffy
// platforms this is `diff * 1e9 / HZ` with a 128-bit intermediate; on
// Windows it is `diff * 100`. Pure: tick diff in, ns out.
uint64_t BusyTicksToNs(uint64_t tick_diff);

}  // namespace cpu
