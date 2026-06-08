#include "core/platform.h"

#if IS_BSD

#  include "stat.h"

#  include <sys/sysctl.h>
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
// Unit conversion uses util::GetJiffyMillisecond(), which is
// sysconf(_SC_CLK_TCK)-based and works on BSD just as on Linux/macOS.

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
  // jiffy -> ns. GetJiffyMillisecond() is sysconf(_SC_CLK_TCK)-based.
  return busy_jiffies * static_cast<uint64_t>(util::GetJiffyMillisecond()) * 1'000'000ULL;
}

}  // namespace cpu

#endif  // IS_BSD
