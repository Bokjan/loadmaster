// Unit tests for cpu::GetBusyTicks and cpu::TicksToMilliseconds.
//
// These two helpers turn a raw CpuStatInfo snapshot into platform-
// agnostic quantities consumed by the CPU scheduler. The platform
// dispatch lives in `stat.cc`; the math we care about here is
// deliberately tiny but easy to break (e.g. someone "fixes" the
// busy-ticks formula by adding `iowait` and silently shifts every
// load measurement). These tests pin the current semantics.
//
// We do NOT exercise GetCpuProcStat() because it pokes at real system
// state (/proc/stat, GetSystemTimes, host_statistics64) which is
// neither deterministic nor portable across CI workers.

#include "cpu/stat.h"

#include <cstdint>

#include <gtest/gtest.h>

namespace {

using cpu::CpuStatInfo;
using cpu::GetBusyTicks;
using cpu::TicksToMilliseconds;

#if !IS_WINDOWS

// Build a snapshot with explicit named fields so it's obvious which
// component the test is exercising.
CpuStatInfo MakeStat(uint64_t user, uint64_t nice, uint64_t system,
                     uint64_t idle, uint64_t iowait, uint64_t irq,
                     uint64_t softirq, uint64_t steal,
                     uint64_t guest, uint64_t guest_nice) {
  CpuStatInfo s;
  s.user = user;
  s.nice = nice;
  s.system = system;
  s.idle = idle;
  s.iowait = iowait;
  s.irq = irq;
  s.softirq = softirq;
  s.steal = steal;
  s.guest = guest;
  s.guest_nice = guest_nice;
  return s;
}

TEST(GetBusyTicksTest, ZeroSnapshotReturnsZero) {
  CpuStatInfo s{};
  EXPECT_EQ(GetBusyTicks(s), 0u);
}

TEST(GetBusyTicksTest, SumsOnlyUserNiceSystem) {
  // The "concerned busy" definition is user + nice + system.
  // Idle / iowait / irq / softirq / steal / guest / guest_nice must
  // NOT contribute -- pin this so a refactor doesn't silently broaden
  // the formula.
  const CpuStatInfo s = MakeStat(/*user*/ 100, /*nice*/ 20, /*system*/ 30,
                                 /*idle*/ 1'000'000, /*iowait*/ 1'000'000,
                                 /*irq*/ 1'000'000, /*softirq*/ 1'000'000,
                                 /*steal*/ 1'000'000,
                                 /*guest*/ 1'000'000, /*guest_nice*/ 1'000'000);
  EXPECT_EQ(GetBusyTicks(s), 100u + 20u + 30u);
}

TEST(GetBusyTicksTest, OnlyUser) {
  const CpuStatInfo s = MakeStat(42, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  EXPECT_EQ(GetBusyTicks(s), 42u);
}

TEST(GetBusyTicksTest, OnlyNice) {
  const CpuStatInfo s = MakeStat(0, 7, 0, 0, 0, 0, 0, 0, 0, 0);
  EXPECT_EQ(GetBusyTicks(s), 7u);
}

TEST(GetBusyTicksTest, OnlySystem) {
  const CpuStatInfo s = MakeStat(0, 0, 99, 0, 0, 0, 0, 0, 0, 0);
  EXPECT_EQ(GetBusyTicks(s), 99u);
}

TEST(GetBusyTicksTest, HandlesLargeUnsignedValues) {
  // Long-running systems can have huge jiffy counters; make sure the
  // sum doesn't accidentally overflow into a narrower type.
  constexpr uint64_t kBig = uint64_t{1} << 50;
  const CpuStatInfo s = MakeStat(kBig, kBig, kBig, 0, 0, 0, 0, 0, 0, 0);
  EXPECT_EQ(GetBusyTicks(s), kBig * 3u);
}

#else  // IS_WINDOWS

TEST(GetBusyTicksTest, WindowsReturnsConcernedFieldDirectly) {
  CpuStatInfo s{};
  s.windows_concerned_100ns = 1'234'567;
  EXPECT_EQ(GetBusyTicks(s), 1'234'567u);
}

TEST(GetBusyTicksTest, WindowsZeroSnapshotReturnsZero) {
  CpuStatInfo s{};
  EXPECT_EQ(GetBusyTicks(s), 0u);
}

#endif  // !IS_WINDOWS

// ---- TicksToMilliseconds -------------------------------------------------
//
// On Linux/macOS this multiplies ticks by GetJiffyMillisecond() (a
// process-wide cached value derived from sysconf(_SC_CLK_TCK), typically
// 10 ms with HZ=100). On Windows it converts 100ns ticks to ms.
//
// We don't hardcode the jiffy length -- it's a platform property -- but
// we lock in the structural properties: monotonicity, zero maps to
// zero, and the conversion is linear.

TEST(TicksToMillisecondsTest, ZeroMapsToZero) {
  EXPECT_EQ(TicksToMilliseconds(0), 0u);
}

TEST(TicksToMillisecondsTest, IsMonotonicallyNonDecreasing) {
  uint64_t prev = TicksToMilliseconds(0);
  for (uint64_t ticks : {uint64_t{1}, uint64_t{10}, uint64_t{100},
                         uint64_t{1000}, uint64_t{100'000}}) {
    const uint64_t cur = TicksToMilliseconds(ticks);
    EXPECT_GE(cur, prev) << "ticks=" << ticks;
    prev = cur;
  }
}

TEST(TicksToMillisecondsTest, IsLinear) {
  // f(a + b) == f(a) + f(b) for any pure multiplication by a constant.
  // Pick values that comfortably avoid 64-bit overflow on any platform.
  const uint64_t a = 137;
  const uint64_t b = 4096;
  EXPECT_EQ(TicksToMilliseconds(a + b),
            TicksToMilliseconds(a) + TicksToMilliseconds(b));
}

TEST(TicksToMillisecondsTest, ScalesByConstantFactor) {
  // f(k * x) == k * f(x). Implies the conversion factor is constant
  // across calls (i.e. cached, not re-read from sysconf every time).
  const uint64_t x = 13;
  const uint64_t k = 1000;
  EXPECT_EQ(TicksToMilliseconds(k * x), k * TicksToMilliseconds(x));
}

}  // namespace
