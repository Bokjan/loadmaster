// Unit tests for util::GetJiffyMillisecond (POSIX-only).
//
// GetJiffyMillisecond returns the length of one kernel jiffy in
// milliseconds, derived from sysconf(_SC_CLK_TCK). It is called from
// cpu::stat_linux.cc and cpu::stat_macos.cc to convert jiffies to
// nanoseconds. The function caches its result behind a function-
// local static, so:
//
//   * the first call drives the sysconf() syscall,
//   * every subsequent call returns the cached value,
//   * the cached value is always positive (a non-positive sysconf()
//     result falls back to 10 ms / HZ=100).
//
// We don't pin a specific value (HZ varies: 100 / 250 / 1000 are all
// common in production kernels). We do pin the contract: positive,
// stable across calls, and within a plausible range.
//
// Compiles to zero tests on Windows. clock.cc itself is guarded by
// `#if !IS_WINDOWS`, so there is no symbol to call there.

#include "core/platform.h"

#if !IS_WINDOWS

#  include "util/clock.h"

#  include <gtest/gtest.h>

namespace {

TEST(GetJiffyMillisecondTest, IsStrictlyPositive) {
  // Every CPU-busy reading on POSIX divides by this value's inverse
  // (jiffies * ms_per_jiffy * 1e6). A zero or negative jiffy length
  // would either divide-by-zero downstream or produce nonsense
  // negative ns counts. The fallback path (`return 10` in clock.cc)
  // guarantees positivity even on exotic kernels.
  EXPECT_GT(util::GetJiffyMillisecond(), 0);
}

TEST(GetJiffyMillisecondTest, IsStableAcrossCalls) {
  // The function caches its result in a function-local static; once
  // initialised the value must never change. Driver code (stat_linux,
  // stat_macos) assumes this so it can multiply jiffies by a
  // constant factor on every reading without re-querying sysconf.
  const int a = util::GetJiffyMillisecond();
  const int b = util::GetJiffyMillisecond();
  const int c = util::GetJiffyMillisecond();
  EXPECT_EQ(a, b);
  EXPECT_EQ(b, c);
}

TEST(GetJiffyMillisecondTest, IsInPlausibleRange) {
  // Real kernels use HZ values of 100, 250, 300, 1000 (and a few
  // exotic ones like 64 or 2000). The corresponding jiffy lengths
  // in milliseconds are:
  //   HZ=2000 -> 0   (integer division of 1000/2000 truncates)
  //   HZ=1000 -> 1
  //   HZ=300  -> 3
  //   HZ=250  -> 4
  //   HZ=100  -> 10
  //   HZ=64   -> 15
  // Plus the fallback path returns 10 if sysconf is unavailable.
  //
  // We accept anything in [1..100] ms: this covers every realistic
  // kernel (including the rare HZ=64) without being so permissive
  // that an arithmetic bug returning, say, 1'000'000 ms would slip
  // through. The HZ=2000 case truncates to 0 and is already caught
  // by IsStrictlyPositive above -- if a host ever does run with
  // HZ>=2000, that's the test that will fail loudly first.
  const int ms = util::GetJiffyMillisecond();
  EXPECT_GE(ms, 1);
  EXPECT_LE(ms, 100) << "implausible jiffy length " << ms << " ms";
}

}  // namespace

#endif  // !IS_WINDOWS
