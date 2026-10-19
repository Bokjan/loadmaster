#include "core/platform.h"

#if IS_MACOS

#  include "stat.h"

#  include <algorithm>
#  include <cstddef>
#  include <cstdint>
#  include <iterator>
#  include <mutex>
#  include <thread>

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
//
// The counters are 32-bit (natural_t) and summed over all CPUs, so at
// 100 Hz they wrap after 2^32 / (100 * cores) seconds -- about 8 days on a
// 64-core machine. They are widened to 64 bits by accumulating each
// state's change modulo 2^32, which is exact as long as two reads are less
// than one wrap apart (the scheduler reads every 100 ms).
std::optional<uint64_t> ReadSystemBusyTicks() {
  host_cpu_load_info_data_t load{};
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  const kern_return_t kr = ::host_statistics64(::mach_host_self(), HOST_CPU_LOAD_INFO,
                                               reinterpret_cast<host_info64_t>(&load), &count);
  if (kr != KERN_SUCCESS) {
    LOG_ERROR("host_statistics64(HOST_CPU_LOAD_INFO) failed: kr=%d", kr);
    return std::nullopt;
  }
  const uint32_t current[] = {load.cpu_ticks[CPU_STATE_USER], load.cpu_ticks[CPU_STATE_NICE],
                              load.cpu_ticks[CPU_STATE_SYSTEM]};

  static std::mutex mutex;
  static bool has_previous = false;
  static uint32_t previous[std::size(current)] = {};
  static uint64_t busy_jiffies = 0;
  const std::lock_guard lock(mutex);
  for (std::size_t i = 0; i < std::size(current); ++i) {
    busy_jiffies += has_previous ? static_cast<uint32_t>(current[i] - previous[i]) : current[i];
    previous[i] = current[i];
  }
  has_previous = true;
  return busy_jiffies;
}

uint64_t BusyTicksToNs(uint64_t tick_diff) {
  // jiffy -> ns. GetClockTicksPerSecond() is available on macOS (clock.cc
  // is compiled for every non-Windows platform).
  return util::TicksToNs(tick_diff, util::GetClockTicksPerSecond());
}

int CoreCount() {
  // macOS has no per-process CPU affinity to honour.
  static const int kCached = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
  return kCached;
}

}  // namespace cpu

#endif  // IS_MACOS
