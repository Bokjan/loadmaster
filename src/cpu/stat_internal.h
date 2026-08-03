// Linux-only internals of cpu::ReadSystemBusyTicks(), exposed in a dedicated
// header so the pure busy-aggregation and unit-conversion logic can be
// exercised directly from unit tests. Do NOT include this from production
// code outside of stat_linux.cc; the public surface is
// `cpu::ReadSystemBusyTicks` / `cpu::BusyTicksToNs` in stat.h.
//
// The entire header is compiled out on non-Linux platforms so it cannot
// accidentally be referenced from portable code.

#pragma once

#include "core/platform.h"

#if IS_LINUX

#  include <cstdint>

namespace cpu::internal {

// Raw /proc/stat aggregate "cpu" line fields, in jiffies. Field order
// matches the kernel's documented layout (see `man 5 proc`).
struct ProcStatFields final {
  uint64_t user = 0;
  uint64_t nice = 0;
  uint64_t system = 0;
  uint64_t idle = 0;
  uint64_t iowait = 0;
  uint64_t irq = 0;
  uint64_t softirq = 0;
  uint64_t steal = 0;
  uint64_t guest = 0;
  uint64_t guest_nice = 0;
};

// "Busy" jiffies = everything the CPU was NOT idle for:
//   user + nice + system + irq + softirq + steal
// idle and iowait are EXCLUDED (both represent the CPU not doing work).
// guest / guest_nice are already accounted for inside user / nice by the
// kernel, so they are not added again. Pure: jiffies in, jiffies out.
uint64_t BusyJiffies(const ProcStatFields &f);

// Convert a jiffy DIFF to nanoseconds via util::GetJiffyFrequency()
// (diff * 1e9 / HZ), using a 128-bit intermediate so a large stalled-gap
// diff still can't overflow before the divide. Pure. Only safe for diffs,
// not cumulative values -- see ReadSystemBusyTicks.
uint64_t JiffiesToNs(uint64_t jiffies);

}  // namespace cpu::internal

#endif  // IS_LINUX
