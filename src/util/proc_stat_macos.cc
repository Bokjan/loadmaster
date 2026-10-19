#include "core/platform.h"

#if IS_MACOS

#  include "proc_stat.h"

#  include <libproc.h>
#  include <mach/mach_time.h>
#  include <sys/resource.h>

#  include "util/log.h"

namespace util {

namespace {

// rusage_info's ri_user_time / ri_system_time are in Mach absolute-time
// units, not nanoseconds. On Intel Macs the timebase is 1/1 so the two
// coincide, which is why this is easy to miss; on Apple Silicon it is
// 125/3 (~41.67 ns per unit), so an unscaled read under-reports our own
// CPU time ~42x and the controller then mistakes our load for load from
// other processes. Cached: the timebase is fixed for the machine.
const mach_timebase_info_data_t &Timebase() {
  static const mach_timebase_info_data_t kCached = []() {
    mach_timebase_info_data_t tb{};
    if (::mach_timebase_info(&tb) != KERN_SUCCESS || tb.numer == 0 || tb.denom == 0) {
      LOG_WARN("mach_timebase_info failed, assuming a 1/1 timebase");
      tb.numer = 1;
      tb.denom = 1;
    }
    return tb;
  }();
  return kCached;
}

}  // namespace

// macOS has no /proc; query libproc for the absolute CPU time of this
// process (user + system, all threads), in Mach absolute-time units.
std::optional<uint64_t> ProcStat::ReadProcessCpuTicks() const {
  rusage_info_current rusage{};
  if (::proc_pid_rusage(pid_, RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t *>(&rusage)) != 0) {
    LOG_ERROR("proc_pid_rusage failed for pid %d", pid_);
    return std::nullopt;
  }
  return rusage.ri_user_time + rusage.ri_system_time;
}

uint64_t ProcessCpuTicksToNs(uint64_t tick_diff) {
  const mach_timebase_info_data_t &tb = Timebase();
  // Split like util::TicksToNs so `units * numer` cannot overflow.
  return tick_diff / tb.denom * tb.numer + tick_diff % tb.denom * tb.numer / tb.denom;
}

}  // namespace util

#endif  // IS_MACOS
