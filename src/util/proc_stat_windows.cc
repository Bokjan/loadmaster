#include "core/platform.h"

#if IS_WINDOWS

#  include "proc_stat.h"

#  include "util/log.h"
#  include "util/win_util.h"

namespace util {

// Windows reports per-process kernel / user time as FILETIMEs (100ns ticks).
// Their sum is the process's total CPU time. Returned as raw 100ns ticks --
// the native unit -- so the caller diffs in tick space (100ns ticks do not
// overflow uint64 in any realistic uptime, unlike the cumulative-ns path
// which wrapped after ~9 years on a 64-core-pegged process) and only
// converts the small diff via ProcessCpuTicksToNs.
std::optional<uint64_t> ProcStat::ReadProcessCpuTicks() const {
  FILETIME creation, exit, kernel, user;
  if (!GetProcessTimes(process_handle_, &creation, &exit, &kernel, &user)) {
    LOG_ERROR("failed to invoke GetProcessTimes");
    return std::nullopt;
  }
  return FiletimeTo100Ns(&kernel) + FiletimeTo100Ns(&user);
}

uint64_t ProcessCpuTicksToNs(uint64_t tick_diff) {
  // 100ns ticks -> nanoseconds.
  return tick_diff * 100ULL;
}

}  // namespace util

#endif  // IS_WINDOWS
