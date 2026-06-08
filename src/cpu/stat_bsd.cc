#include "core/platform.h"

#if IS_BSD

#  include "stat.h"

#  include <sys/param.h>
#  include <sys/sysctl.h>
#  include <sys/types.h>

#  include <cstdint>
#  include <cstring>

#  include "util/clock.h"
#  include "util/log.h"

namespace cpu {

// FreeBSD / DragonFly (and OpenBSD / NetBSD which share the same MIB
// shape) expose system-wide CPU time counters through the
// {CTL_KERN, KERN_CP_TIME} sysctl. The result is an array of `long`
// counters in clock ticks, indexed by the CPUSTATES enum from
// <sys/resource.h> (CP_USER, CP_NICE, CP_SYS, CP_INTR, CP_IDLE).
//
// "Busy = not idle" follows the same convention as the Linux backend:
//   busy = user + nice + sys + intr
// (CP_IDLE is the only state we subtract out.) The kernel does not
// expose an iowait equivalent on BSD, so there is nothing analogous
// to Linux's iowait-exclusion to worry about.
//
// Unit conversion uses util::GetJiffyMillisecond() which goes through
// sysconf(_SC_CLK_TCK); BSD honors that the same way Linux/macOS do.

// Only FreeBSD and DragonFly are wired up for now. OpenBSD/NetBSD share
// the same MIB, but their kinfo_proc layouts differ enough that we want
// to validate the full ProcStat path before claiming support. If you
// land here on Open/NetBSD, the build is intentionally failing: see
// stat_bsd.cc / proc_stat_bsd.cc for the (small) remaining work, then
// remove this guard.
#  if !defined(__FreeBSD__) && !defined(__DragonFly__)
#    error "loadmaster: only FreeBSD / DragonFly are wired up in the BSD backend so far"
#  endif

std::optional<uint64_t> ReadSystemBusyNs() {
  // CPUSTATES is defined in <sys/resource.h> (pulled in via <sys/param.h>
  // on FreeBSD); it is 5 on all current BSDs. We size the buffer using
  // it so a hypothetical future kernel addition is caught at compile time.
  long cp_time[CPUSTATES] = {0};
  std::size_t len = sizeof(cp_time);
  // KERN_CP_TIME, not "kern.cp_time" by name: sysctlbyname would work
  // too, but the numeric MIB is portable across all BSDs without an
  // extra string lookup.
  int mib[2] = {CTL_KERN, KERN_CP_TIME};
  if (::sysctl(mib, 2, cp_time, &len, nullptr, 0) != 0) {
    LOG_ERROR("sysctl(kern.cp_time) failed");
    return std::nullopt;
  }
  if (len < sizeof(cp_time)) {
    // Defensive: a kernel returning fewer states than CPUSTATES would
    // leave the tail uninitialized. Treat as a hard read failure rather
    // than silently underreporting busy time.
    LOG_ERROR("sysctl(kern.cp_time) short read: %zu < %zu", len, sizeof(cp_time));
    return std::nullopt;
  }
  const uint64_t busy_jiffies =
      static_cast<uint64_t>(cp_time[CP_USER]) + static_cast<uint64_t>(cp_time[CP_NICE]) +
      static_cast<uint64_t>(cp_time[CP_SYS]) + static_cast<uint64_t>(cp_time[CP_INTR]);
  // jiffy -> ns. GetJiffyMillisecond() is sysconf(_SC_CLK_TCK)-based and
  // works on BSD just as on Linux/macOS.
  return busy_jiffies * static_cast<uint64_t>(util::GetJiffyMillisecond()) * 1'000'000ULL;
}

}  // namespace cpu

#endif  // IS_BSD
