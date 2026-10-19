// Unit tests for the pure helpers behind cpu::ReadSystemBusyNs() on Linux:
//   * internal::BusyJiffies  -- aggregates the /proc/stat cpu-line fields
//     into a single "busy" (i.e. not-idle) jiffy count.
//   * internal::JiffiesToNs  -- linear jiffy -> nanosecond conversion.
//
// The platform read (opening /proc/stat, GetSystemTimes, host_statistics64)
// is deliberately NOT exercised: it pokes at real system state that is
// neither deterministic nor portable across CI workers. We only pin the
// math, which is tiny but easy to break -- e.g. someone "fixes" the busy
// formula by folding iowait back in and silently shifts every load reading.
//
// Linux-only: the busy aggregation is defined in stat_linux.cc and exposed
// through cpu/stat_internal.h, both of which compile out elsewhere. The
// whole TU is guarded by IS_LINUX so it builds to zero tests on macOS /
// Windows (matching util_proc_stat_internal_test).

#include "core/platform.h"

#if IS_LINUX

#  include "cpu/stat_internal.h"

#  include <cstdint>
#  include <cstdio>
#  include <cstring>
#  include <optional>
#  include <vector>

#  include "util/clock.h"

#  include <gtest/gtest.h>

namespace {

using cpu::internal::BusyJiffies;
using cpu::internal::JiffiesToNs;
using cpu::internal::ParseCgroupCpuMax;
using cpu::internal::ProcStatFields;
using cpu::internal::ReadBusyJiffies;

// Build a fields snapshot with explicit named members so it's obvious which
// component each test exercises.
ProcStatFields MakeFields(uint64_t user, uint64_t nice, uint64_t system, uint64_t idle,
                          uint64_t iowait, uint64_t irq, uint64_t softirq, uint64_t steal,
                          uint64_t guest, uint64_t guest_nice) {
  ProcStatFields f;
  f.user = user;
  f.nice = nice;
  f.system = system;
  f.idle = idle;
  f.iowait = iowait;
  f.irq = irq;
  f.softirq = softirq;
  f.steal = steal;
  f.guest = guest;
  f.guest_nice = guest_nice;
  return f;
}

// ---- BusyJiffies ----------------------------------------------------------

TEST(BusyJiffiesTest, ZeroSnapshotReturnsZero) {
  ProcStatFields f{};
  EXPECT_EQ(BusyJiffies(f), 0u);
}

TEST(BusyJiffiesTest, SumsUserNiceSystemIrqSoftirqSteal) {
  // The "not idle" definition: user + nice + system + irq + softirq + steal.
  const ProcStatFields f = MakeFields(/*user*/ 100, /*nice*/ 20, /*system*/ 30,
                                      /*idle*/ 0, /*iowait*/ 0, /*irq*/ 1,
                                      /*softirq*/ 2, /*steal*/ 4,
                                      /*guest*/ 0, /*guest_nice*/ 0);
  EXPECT_EQ(BusyJiffies(f), 100u + 20u + 30u + 1u + 2u + 4u);
}

TEST(BusyJiffiesTest, ExcludesIdleAndIowait) {
  // idle and iowait are NOT busy -- both represent the CPU not doing work.
  // Pin this so a refactor doesn't silently fold them back in.
  const ProcStatFields f = MakeFields(/*user*/ 5, /*nice*/ 0, /*system*/ 0,
                                      /*idle*/ 1'000'000, /*iowait*/ 1'000'000,
                                      /*irq*/ 0, /*softirq*/ 0, /*steal*/ 0,
                                      /*guest*/ 0, /*guest_nice*/ 0);
  EXPECT_EQ(BusyJiffies(f), 5u);
}

TEST(BusyJiffiesTest, ExcludesGuestAndGuestNice) {
  // guest / guest_nice are already folded into user / nice by the kernel,
  // so adding them again would double-count. They must not contribute.
  const ProcStatFields f = MakeFields(/*user*/ 5, /*nice*/ 0, /*system*/ 0,
                                      /*idle*/ 0, /*iowait*/ 0, /*irq*/ 0,
                                      /*softirq*/ 0, /*steal*/ 0,
                                      /*guest*/ 1'000'000, /*guest_nice*/ 1'000'000);
  EXPECT_EQ(BusyJiffies(f), 5u);
}

TEST(BusyJiffiesTest, IrqSoftirqStealEachContribute) {
  EXPECT_EQ(BusyJiffies(MakeFields(0, 0, 0, 0, 0, 7, 0, 0, 0, 0)), 7u);  // irq
  EXPECT_EQ(BusyJiffies(MakeFields(0, 0, 0, 0, 0, 0, 9, 0, 0, 0)), 9u);  // softirq
  EXPECT_EQ(BusyJiffies(MakeFields(0, 0, 0, 0, 0, 0, 0, 3, 0, 0)), 3u);  // steal
}

TEST(BusyJiffiesTest, HandlesLargeUnsignedValues) {
  // Long-running systems can have huge jiffy counters; make sure the sum
  // doesn't accidentally overflow into a narrower type.
  constexpr uint64_t kBig = uint64_t{1} << 50;
  const ProcStatFields f = MakeFields(kBig, kBig, kBig, 0, 0, kBig, kBig, kBig, 0, 0);
  EXPECT_EQ(BusyJiffies(f), kBig * 6u);
}

// ---- JiffiesToNs ----------------------------------------------------------
//
// jiffies -> nanoseconds goes through util::TicksToNs at USER_HZ
// (GetClockTicksPerSecond()). We don't hardcode USER_HZ (it's a platform
// property) but lock in the structural properties: zero maps to zero,
// monotonicity, linearity, constant scaling, and the exact factor.

TEST(JiffiesToNsTest, ZeroMapsToZero) {
  EXPECT_EQ(JiffiesToNs(0), 0u);
}

TEST(JiffiesToNsTest, MatchesExpectedFactor) {
  const uint64_t hz = util::GetClockTicksPerSecond();
  EXPECT_EQ(JiffiesToNs(hz), 1'000'000'000u);
  EXPECT_EQ(JiffiesToNs(123), util::TicksToNs(123, hz));
}

TEST(JiffiesToNsTest, IsMonotonicallyNonDecreasing) {
  uint64_t prev = JiffiesToNs(0);
  for (uint64_t j : {uint64_t{1}, uint64_t{10}, uint64_t{100}, uint64_t{1000}, uint64_t{100'000}}) {
    const uint64_t cur = JiffiesToNs(j);
    EXPECT_GE(cur, prev) << "jiffies=" << j;
    prev = cur;
  }
}

TEST(JiffiesToNsTest, IsLinear) {
  // f(a + b) == f(a) + f(b). Pure multiplication, so any operands work.
  const uint64_t a = 13;
  const uint64_t b = 409;
  EXPECT_EQ(JiffiesToNs(a + b), JiffiesToNs(a) + JiffiesToNs(b));
}

TEST(JiffiesToNsTest, ScalesByConstantFactor) {
  // f(k * x) == k * f(x): implies the factor is constant (cached, not
  // re-read from sysconf every call).
  const uint64_t x = 137;
  const uint64_t k = 1000;
  EXPECT_EQ(JiffiesToNs(k * x), k * JiffiesToNs(x));
}

// ---- ReadBusyJiffies --------------------------------------------------------
//
// Feeds /proc/stat-shaped text through fmemopen. Busy per line is
// user + nice + system + irq + softirq + steal.

constexpr char kProcStat[] =
    "cpu  60 0 30 900 5 1 2 0 0 0\n"
    "cpu0 10 0 5 300 1 0 1 0 0 0\n"
    "cpu1 20 0 10 300 2 1 0 0 0 0\n"
    "cpu2 30 0 15 300 2 0 1 0 0 0\n"
    "intr 12345 0 0\n"
    "ctxt 999\n";

std::optional<uint64_t> ReadFrom(const char *text, const std::vector<bool> *allowed) {
  FILE *fp = fmemopen(const_cast<char *>(text), std::strlen(text), "r");
  if (fp == nullptr) {
    return std::nullopt;
  }
  const auto r = ReadBusyJiffies(fp, allowed);
  std::fclose(fp);
  return r;
}

TEST(ReadBusyJiffiesTest, AggregateLineWhenUnrestricted) {
  EXPECT_EQ(ReadFrom(kProcStat, nullptr), std::optional<uint64_t>(60 + 30 + 1 + 2));
}

TEST(ReadBusyJiffiesTest, SumsOnlyAllowedCpus) {
  const std::vector<bool> allowed = {false, true, true};  // cpu1 + cpu2
  EXPECT_EQ(ReadFrom(kProcStat, &allowed), std::optional<uint64_t>((20 + 10 + 1) + (30 + 15 + 1)));
}

TEST(ReadBusyJiffiesTest, CpusBeyondMaskAreExcluded) {
  const std::vector<bool> allowed = {true};  // cpu0 only; cpu1/cpu2 outside the vector
  EXPECT_EQ(ReadFrom(kProcStat, &allowed), std::optional<uint64_t>(10 + 5 + 1));
}

TEST(ReadBusyJiffiesTest, AcceptsOldKernelWithoutGuestFields) {
  EXPECT_EQ(ReadFrom("cpu  7 1 2 50 0 0 0 0\n", nullptr), std::optional<uint64_t>(10));
}

TEST(ReadBusyJiffiesTest, NoMatchingLineFails) {
  const std::vector<bool> allowed = {false, false, false, true};  // cpu3 absent
  EXPECT_FALSE(ReadFrom(kProcStat, &allowed).has_value());
  EXPECT_FALSE(ReadFrom("intr 1 2 3\n", nullptr).has_value());
}

TEST(ReadBusyJiffiesTest, MalformedLineFails) {
  EXPECT_FALSE(ReadFrom("cpu  1 2 x\n", nullptr).has_value());
}

// ---- ParseCgroupCpuMax -------------------------------------------------------

TEST(ParseCgroupCpuMaxTest, UnlimitedIsNullopt) {
  EXPECT_FALSE(ParseCgroupCpuMax("max 100000\n").has_value());
}

TEST(ParseCgroupCpuMaxTest, WholeCpus) {
  EXPECT_EQ(ParseCgroupCpuMax("200000 100000\n"), std::optional<int>(2));
}

TEST(ParseCgroupCpuMaxTest, FractionalQuotaRoundsUp) {
  EXPECT_EQ(ParseCgroupCpuMax("150000 100000"), std::optional<int>(2));
  EXPECT_EQ(ParseCgroupCpuMax("50000 100000"), std::optional<int>(1));
}

TEST(ParseCgroupCpuMaxTest, MalformedIsNullopt) {
  EXPECT_FALSE(ParseCgroupCpuMax("").has_value());
  EXPECT_FALSE(ParseCgroupCpuMax("garbage").has_value());
  EXPECT_FALSE(ParseCgroupCpuMax("100000 0").has_value());
}

}  // namespace

#endif  // IS_LINUX
