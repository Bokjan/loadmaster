#include "core/platform.h"

#if IS_BSD

#  include "proc_stat.h"

#  include <sys/param.h>
#  include <sys/sysctl.h>
#  include <sys/types.h>
#  include <sys/user.h>

#  include <cstddef>
#  include <cstdint>
#  include <cstring>

#  include "util/log.h"

namespace util {

// BSD has no /proc; we get per-process CPU accounting via the
// {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid} sysctl, which fills a
// `struct kinfo_proc`. On FreeBSD and DragonFly the relevant field is
// `ki_runtime`, which the kernel documents as the **microseconds** of
// CPU time accumulated by the process (user + system combined).
//
// We return ki_runtime raw (microseconds, the native unit) so the caller
// diffs in tick space -- microseconds do not overflow uint64 in any
// realistic uptime, unlike the cumulative-nanosecond path which wrapped
// after ~9 years on a 64-core-pegged process -- and only converts the
// small diff via ProcessCpuTicksToNs.
//
// Note: ki_runtime aggregates across all threads of the process, which
// is what we want -- the rest of loadmaster compares this number
// against wall-clock elapsed time to derive a load percentage.

// Only FreeBSD and DragonFly are wired up. See stat_bsd.cc for the
// matching note on Open/NetBSD.
#  if !defined(__FreeBSD__) && !defined(__DragonFly__)
#    error "loadmaster: only FreeBSD / DragonFly are wired up in the BSD backend so far"
#  endif

std::optional<uint64_t> ProcStat::ReadProcessCpuTicks() const {
  struct kinfo_proc kp{};
  std::size_t len = sizeof(kp);
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid_};
  if (::sysctl(mib, 4, &kp, &len, nullptr, 0) != 0) {
    LOG_ERROR("sysctl(kern.proc.pid) failed for pid %d", pid_);
    return std::nullopt;
  }
  // The kernel will return a smaller buffer than sizeof(kinfo_proc) if
  // the running kernel was built against an older struct layout (the
  // tail is just zero-padded for the caller). We only touch ki_runtime,
  // which has been at a stable offset for many releases, so a short
  // read is fine -- just sanity-check that we got at least far enough.
  if (len < offsetof(struct kinfo_proc, ki_runtime) + sizeof(kp.ki_runtime)) {
    LOG_ERROR("sysctl(kern.proc.pid) short read: %zu bytes for pid %d", len, pid_);
    return std::nullopt;
  }
  // ki_runtime is microseconds on FreeBSD/DragonFly -- native tick unit.
  return static_cast<uint64_t>(kp.ki_runtime);
}

uint64_t ProcessCpuTicksToNs(uint64_t tick_diff) {
  // microseconds -> nanoseconds.
  return tick_diff * 1000ULL;
}

}  // namespace util

#endif  // IS_BSD
