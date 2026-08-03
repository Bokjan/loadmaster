#include "core/platform.h"

#if IS_MACOS

#  include "proc_stat.h"

#  include <libproc.h>
#  include <sys/resource.h>

#  include "util/log.h"

namespace util {

// macOS has no /proc; query libproc for the absolute CPU time of this
// process. rusage_info_v2 exposes ri_user_time / ri_system_time directly in
// nanoseconds -- the kernel gives no coarser unit, so the "native tick"
// here IS the nanosecond. The cumulative counter is therefore still
// bounded by the kernel's 64-bit ns width (inherent, ~9 years on a process
// pegging 64 cores); diffing in this unit is a no-op for wraparound but
// keeps the interface uniform with the other backends.
std::optional<uint64_t> ProcStat::ReadProcessCpuTicks() const {
  rusage_info_current rusage{};
  if (::proc_pid_rusage(pid_, RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t *>(&rusage)) != 0) {
    LOG_ERROR("proc_pid_rusage failed for pid %d", pid_);
    return std::nullopt;
  }
  return rusage.ri_user_time + rusage.ri_system_time;
}

uint64_t ProcessCpuTicksToNs(uint64_t tick_diff) {
  // Native unit is already nanoseconds; identity conversion.
  return tick_diff;
}

}  // namespace util

#endif  // IS_MACOS
