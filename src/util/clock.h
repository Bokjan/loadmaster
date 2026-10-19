#pragma once

#include <cstdint>

// We only need IS_WINDOWS here, so include the platform-detection header
// directly rather than core/constants.h. The latter pulls in a much
// larger set of project-wide constants we don't use, and depending on
// it just for a transitive macro is the kind of include-what-you-use
// trap clangd flags as `unused-includes`.
#include "core/platform.h"

namespace util {

// Convert `ticks` of a clock running at `hz` ticks per second into
// nanoseconds. Exact for any hz that divides 1e9 and otherwise rounds
// down by less than 1 ns -- unlike a precomputed integer "ms per tick"
// factor, which truncates e.g. 1000/128 = 7.8125 ms down to 7 ms (-10%)
// and collapses to 0 for hz > 1000. Splitting into whole seconds plus a
// remainder keeps `ticks * 1e9` from overflowing uint64_t for any
// realistic cumulative counter. `hz` must be positive.
constexpr uint64_t TicksToNs(uint64_t ticks, uint64_t hz) {
  constexpr uint64_t kNsPerSecond = 1'000'000'000ULL;
  return ticks / hz * kNsPerSecond + ticks % hz * kNsPerSecond / hz;
}

#if !IS_WINDOWS
// Clock ticks per second from sysconf(_SC_CLK_TCK) -- the unit of the
// Linux /proc/stat and /proc/<pid>/stat counters and of macOS
// host_cpu_load_info. Cached after the first call. Always returns a
// positive value (falls back to 100 if the syscall is unavailable).
//
// NOT the unit of FreeBSD's kern.cp_time, which counts statclock ticks
// (kern.clockrate.stathz); see cpu/stat_bsd.cc.
uint64_t GetClockTicksPerSecond();
#endif

}  // namespace util
