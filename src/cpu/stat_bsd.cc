#include "core/platform.h"

#if IS_BSD

#  include "stat.h"

#  include <algorithm>
#  include <thread>

#  include <sys/sysctl.h>
#  include <sys/time.h>
#  include <sys/types.h>

#  include <cstddef>
#  include <cstdint>

#  include "util/clock.h"
#  include "util/log.h"

namespace cpu {

// FreeBSD / DragonFly expose system-wide CPU time counters as the
// "kern.cp_time" sysctl, returning an array of `long` counters in
// clock ticks indexed by CPU state. We deliberately use the string
// (sysctlbyname) form rather than the numeric MIB or the named
// CPUSTATES / CP_USER macros from <sys/resource.h>: those macros are
// gated behind _KERNEL on FreeBSD's userspace headers and therefore
// not visible to ordinary application code, and KERN_CP_TIME has had
// availability gaps across BSD versions. The string form is the
// stable userspace-facing interface documented in `sysctl(3)`.
//
// "Busy = not idle" follows the same convention as the Linux backend:
//   busy = user + nice + sys + intr
// (Idle is the only state we subtract out.) The kernel does not
// expose an iowait equivalent on BSD, so there is nothing analogous
// to Linux's iowait-exclusion to worry about.
//
// Unit: kern.cp_time is advanced by statclock(), so each count is one
// tick of the *statistics* clock, kern.clockrate.stathz (typically 127
// on FreeBSD amd64) -- NOT sysconf(_SC_CLK_TCK), which reports the
// compile-time CLK_TCK constant (128). Using the latter under-reads busy
// time by ~1%, and the old truncated "1000/128 = 7 ms per tick" factor
// under-read it by ~11%, while the per-process side (ki_runtime, in
// microseconds) stayed exact -- which skewed the "load from other
// processes" estimate and made the controller overshoot its target.

// Only FreeBSD and DragonFly are wired up for now. OpenBSD/NetBSD
// share the same MIB shape but the rest of the ProcStat path differs
// enough that they need separate validation. If you land here on
// Open/NetBSD, the build is intentionally failing: see this file and
// proc_stat_bsd.cc for the (small) remaining work, then remove this
// guard.
#  if !defined(__FreeBSD__) && !defined(__DragonFly__)
#    error "loadmaster: only FreeBSD / DragonFly are wired up in the BSD backend so far"
#  endif

namespace {

// Locally-defined indices into the kern.cp_time array. These match
// the kernel's CPUSTATES enum (sys/resource.h) verbatim, but we don't
// pull that header in because its definitions are gated behind
// _KERNEL in userspace builds. The layout has been stable since at
// least FreeBSD 5.x and is part of the kern.cp_time userspace ABI:
// changing it would break every existing top(1) / vmstat(1) / etc.
constexpr std::size_t kCpStateUser = 0;
constexpr std::size_t kCpStateNice = 1;
constexpr std::size_t kCpStateSys  = 2;
constexpr std::size_t kCpStateIntr = 3;
constexpr std::size_t kCpStateIdle = 4;
constexpr std::size_t kCpuStateCount = 5;

// Frequency of the clock that advances kern.cp_time. The kernel falls
// back to `hz` when no separate statclock is configured (stathz == 0),
// and so do we. Cached: the rate is fixed at boot.
uint64_t StatClockHz() {
  static const uint64_t kCached = []() -> uint64_t {
    struct clockinfo ci{};
    std::size_t len = sizeof(ci);
    if (::sysctlbyname("kern.clockrate", &ci, &len, nullptr, 0) != 0 || len < sizeof(ci)) {
      LOG_WARN("sysctlbyname(kern.clockrate) failed, assuming stathz=128");
      return 128;
    }
    if (ci.stathz > 0) {
      return static_cast<uint64_t>(ci.stathz);
    }
    return ci.hz > 0 ? static_cast<uint64_t>(ci.hz) : 128;
  }();
  return kCached;
}

}  // namespace

std::optional<uint64_t> ReadSystemBusyNs() {
  long cp_time[kCpuStateCount] = {0};
  std::size_t len = sizeof(cp_time);
  if (::sysctlbyname("kern.cp_time", cp_time, &len, nullptr, 0) != 0) {
    LOG_ERROR("sysctlbyname(kern.cp_time) failed");
    return std::nullopt;
  }
  if (len < sizeof(cp_time)) {
    // Defensive: a kernel returning fewer states than we expect would
    // leave the tail uninitialized. Treat as a hard read failure
    // rather than silently underreporting busy time.
    LOG_ERROR("sysctlbyname(kern.cp_time) short read: %zu < %zu", len, sizeof(cp_time));
    return std::nullopt;
  }
  // Suppress -Wunused-const-variable for kCpStateIdle. We deliberately
  // don't read idle here (busy = total - idle would just be a longer
  // way to say the same thing), but keeping the named index alongside
  // the others makes the layout self-documenting.
  (void)kCpStateIdle;
  const uint64_t busy_jiffies =
      static_cast<uint64_t>(cp_time[kCpStateUser]) +
      static_cast<uint64_t>(cp_time[kCpStateNice]) +
      static_cast<uint64_t>(cp_time[kCpStateSys])  +
      static_cast<uint64_t>(cp_time[kCpStateIntr]);
  // statclock tick -> ns.
  return util::TicksToNs(busy_jiffies, StatClockHz());
}

int CoreCount() {
  // cpuset(1) restrictions are not consulted yet.
  static const int kCached = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
  return kCached;
}

}  // namespace cpu

#endif  // IS_BSD
