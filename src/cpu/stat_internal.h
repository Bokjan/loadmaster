// Linux-only internals of cpu::ReadSystemBusyNs(), exposed in a dedicated
// header so the pure busy-aggregation and unit-conversion logic can be
// exercised directly from unit tests. Do NOT include this from production
// code outside of stat_linux.cc; the public surface is `cpu::ReadSystemBusyNs`
// in stat.h.
//
// The entire header is compiled out on non-Linux platforms so it cannot
// accidentally be referenced from portable code.

#pragma once

#include "core/platform.h"

#if IS_LINUX

#  include <cstdint>
#  include <cstdio>
#  include <optional>
#  include <string_view>
#  include <vector>

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

// Convert a jiffy count to nanoseconds using GetClockTicksPerSecond(). Pure.
uint64_t JiffiesToNs(uint64_t jiffies);

// Read /proc/stat-formatted content from `fp` and return the total busy
// jiffies. With `allowed == nullptr` the aggregate "cpu" line is used;
// otherwise the per-CPU "cpuN" lines are summed for every N with
// allowed[N] == true (CPUs outside the vector count as not allowed).
// Returns nullopt if the requested lines are missing or malformed.
std::optional<uint64_t> ReadBusyJiffies(FILE *fp, const std::vector<bool> *allowed);

// Parse the content of a cgroup v2 `cpu.max` file ("<quota> <period>" or
// "max <period>") into a CPU count, rounding a fractional quota up.
// Returns nullopt for "max" (unlimited) or malformed input.
std::optional<int> ParseCgroupCpuMax(std::string_view content);

}  // namespace cpu::internal

#endif  // IS_LINUX
