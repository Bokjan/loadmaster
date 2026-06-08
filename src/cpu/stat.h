#pragma once

#include <cstdint>
#include <optional>
#include <thread>

namespace cpu {

inline int CoreCount() { return static_cast<int>(std::thread::hardware_concurrency()); }

// Read the system-wide cumulative "busy" CPU time, in nanoseconds, summed
// across all cores and accumulated since boot. The value is monotonically
// non-decreasing between successive calls on the same machine; callers diff
// two readings to derive a load over an interval.
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
std::optional<uint64_t> ReadSystemBusyNs();

}  // namespace cpu
