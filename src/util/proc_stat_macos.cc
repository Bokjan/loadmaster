#include "core/platform.h"

#if IS_MACOS

#  include "proc_stat.h"

#  include <libproc.h>
#  include <sys/resource.h>

#  include "util/log.h"

namespace util {

// macOS has no /proc; query libproc for the absolute CPU time of this
// process. rusage_info_v2 exposes ri_user_time / ri_system_time directly in
// nanoseconds of machine time, so no unit conversion is needed -- this is
// exactly the common currency the rest of ProcStat now speaks.
std::optional<uint64_t> ProcStat::ReadProcessCpuNs() const {
  rusage_info_current rusage{};
  if (::proc_pid_rusage(pid_, RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t *>(&rusage)) != 0) {
    LOG_ERROR("proc_pid_rusage failed for pid %d", pid_);
    return std::nullopt;
  }
  return rusage.ri_user_time + rusage.ri_system_time;
}

}  // namespace util

#endif  // IS_MACOS
