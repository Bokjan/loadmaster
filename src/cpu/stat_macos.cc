#include "core/platform.h"

#if IS_MACOS

#  include "stat.h"

#  include <mach/mach.h>
#  include <mach/mach_host.h>

#  include "util/clock.h"
#  include "util/log.h"

namespace cpu {

// macOS has no /proc; query the Mach host for system-wide CPU tick counters.
// host_cpu_load_info_data_t reports user / system / idle / nice ticks summed
// across all CPUs, in the same unit as Linux jiffies (driven by
// sysconf(_SC_CLK_TCK), typically 100 Hz on macOS). Mach folds interrupt
// time into the "system" bucket, so "busy = user + nice + system" already
// matches the "not idle" intent without an explicit irq term.
std::optional<uint64_t> ReadSystemBusyTicks() {
  host_cpu_load_info_data_t load{};
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  const kern_return_t kr = ::host_statistics64(::mach_host_self(), HOST_CPU_LOAD_INFO,
                                               reinterpret_cast<host_info64_t>(&load), &count);
  if (kr != KERN_SUCCESS) {
    LOG_ERROR("host_statistics64(HOST_CPU_LOAD_INFO) failed: kr=%d", kr);
    return std::nullopt;
  }
  const uint64_t busy_jiffies = static_cast<uint64_t>(load.cpu_ticks[CPU_STATE_USER]) +
                                static_cast<uint64_t>(load.cpu_ticks[CPU_STATE_NICE]) +
                                static_cast<uint64_t>(load.cpu_ticks[CPU_STATE_SYSTEM]);
  return busy_jiffies;
}

uint64_t BusyTicksToNs(uint64_t tick_diff) {
  // jiffy -> ns: diff * 1e9 / HZ (128-bit intermediate). GetJiffyFrequency
  // is available on macOS (clock.cc is compiled for every non-Windows
  // platform). Only safe for diffs, not cumulative values.
  return static_cast<uint64_t>((static_cast<__uint128_t>(tick_diff) * 1'000'000'000ULL) /
                               static_cast<uint64_t>(util::GetJiffyFrequency()));
}

}  // namespace cpu

#endif  // IS_MACOS
