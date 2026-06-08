#include "core/platform.h"

#if IS_WINDOWS

#  include "proc_stat.h"

#  include "util/log.h"
#  include "util/win_util.h"

namespace util {

// Windows reports per-process kernel / user time as FILETIMEs (100ns ticks).
// Their sum is the process's total CPU time; convert to nanoseconds.
std::optional<uint64_t> ProcStat::ReadProcessCpuNs() const {
  FILETIME creation, exit, kernel, user;
  if (!GetProcessTimes(process_handle_, &creation, &exit, &kernel, &user)) {
    LOG_ERROR("failed to invoke GetProcessTimes");
    return std::nullopt;
  }
  const uint64_t ticks_100ns = FiletimeTo100Ns(&kernel) + FiletimeTo100Ns(&user);
  return ticks_100ns * 100ULL;
}

}  // namespace util

#endif  // IS_WINDOWS
