// Unit tests for util/clock.h.
//
//   * util::TicksToNs (all platforms) -- the tick -> nanosecond
//     conversion every POSIX CPU-stat backend funnels through. Pure
//     arithmetic, so we pin exact values, including the non-integer
//     "ms per tick" rates that the old truncating helper got wrong
//     (FreeBSD's CLK_TCK=128 and stathz=127, HZ>1000).
//   * util::GetClockTicksPerSecond (POSIX only) -- sysconf(_SC_CLK_TCK),
//     cached behind a function-local static. We don't pin a specific
//     value (it is 100 on Linux, 128 on FreeBSD, ...), only the
//     contract: positive, stable across calls, plausible.

#include "util/clock.h"

#include <cstdint>

#include <gtest/gtest.h>

namespace {

using util::TicksToNs;

TEST(TicksToNsTest, ZeroMapsToZero) {
  EXPECT_EQ(TicksToNs(0, 100), 0u);
  EXPECT_EQ(TicksToNs(0, 127), 0u);
}

TEST(TicksToNsTest, ExactForDivisorsOfOneSecond) {
  EXPECT_EQ(TicksToNs(1, 100), 10'000'000u);
  EXPECT_EQ(TicksToNs(123, 100), 1'230'000'000u);
  EXPECT_EQ(TicksToNs(1, 1000), 1'000'000u);
  // 1000/128 ms per tick is 7.8125 ms -- the old integer helper said 7.
  EXPECT_EQ(TicksToNs(1, 128), 7'812'500u);
  EXPECT_EQ(TicksToNs(128, 128), 1'000'000'000u);
  // HZ > 1000: the old helper truncated the per-tick factor to 0.
  EXPECT_EQ(TicksToNs(1, 2000), 500'000u);
}

TEST(TicksToNsTest, NonDivisorRateIsAccurateOverOneSecond) {
  // FreeBSD amd64 stathz. 127 ticks must be exactly one second, and a
  // single tick rounds down by less than one nanosecond.
  EXPECT_EQ(TicksToNs(127, 127), 1'000'000'000u);
  EXPECT_EQ(TicksToNs(1, 127), 7'874'015u);
  EXPECT_EQ(TicksToNs(127 * 3600, 127), 3600ULL * 1'000'000'000ULL);
}

TEST(TicksToNsTest, DoesNotOverflowOnLargeCounters) {
  // ~10 years of 256 cores at 100 Hz: ticks * 1e9 alone would overflow
  // uint64_t, the split seconds + remainder form must not.
  const uint64_t ticks = 100ULL * 256 * 3600 * 24 * 365 * 10;
  EXPECT_EQ(TicksToNs(ticks, 100), ticks * 10'000'000ULL);
}

TEST(TicksToNsTest, IsMonotonicallyNonDecreasing) {
  uint64_t prev = 0;
  for (uint64_t t = 0; t < 1000; ++t) {
    const uint64_t cur = TicksToNs(t, 127);
    EXPECT_GE(cur, prev) << "ticks=" << t;
    prev = cur;
  }
}

#if !IS_WINDOWS

TEST(GetClockTicksPerSecondTest, IsStrictlyPositive) {
  // Used as a divisor by TicksToNs; the fallback path guarantees it.
  EXPECT_GT(util::GetClockTicksPerSecond(), 0u);
}

TEST(GetClockTicksPerSecondTest, IsStableAcrossCalls) {
  const uint64_t a = util::GetClockTicksPerSecond();
  const uint64_t b = util::GetClockTicksPerSecond();
  EXPECT_EQ(a, b);
}

TEST(GetClockTicksPerSecondTest, IsInPlausibleRange) {
  // USER_HZ is 100 on Linux, CLK_TCK is 128 on FreeBSD, 100 on macOS.
  // Accept a generous band that still catches an arithmetic bug.
  const uint64_t hz = util::GetClockTicksPerSecond();
  EXPECT_GE(hz, 10u);
  EXPECT_LE(hz, 10'000u) << "implausible clock tick rate " << hz;
}

#endif  // !IS_WINDOWS

}  // namespace
