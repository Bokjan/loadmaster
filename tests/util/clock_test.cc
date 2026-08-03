// Unit tests for util::GetJiffyFrequency (POSIX-only).
//
// GetJiffyFrequency returns the kernel jiffy frequency (HZ), derived from
// sysconf(_SC_CLK_TCK). It is called from the cpu stat backends to convert
// jiffy DIFFS to nanoseconds as `diff * 1e9 / GetJiffyFrequency()`. The
// function caches its result behind a function-local static, so:
//
//   * the first call drives the sysconf() syscall,
//   * every subsequent call returns the cached value,
//   * the cached value is always positive (a non-positive sysconf()
//     result falls back to 100, i.e. HZ=100).
//
// We don't pin a specific value (HZ varies: 100 / 250 / 300 / 1000 are all
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

TEST(GetJiffyFrequencyTest, IsStrictlyPositive) {
  // Every CPU-busy reading on POSIX divides by this value
  // (diff * 1e9 / HZ). A zero would divide-by-zero downstream; the
  // fallback path (`return 100` in clock.cc) guarantees positivity even
  // on exotic kernels.
  EXPECT_GT(util::GetJiffyFrequency(), 0);
}

TEST(GetJiffyFrequencyTest, IsStableAcrossCalls) {
  // The function caches its result in a function-local static; once
  // initialised the value must never change. Driver code (stat_linux,
  // stat_macos, stat_bsd) assumes this so it can multiply jiffies by a
  // constant factor on every reading without re-querying sysconf.
  const long a = util::GetJiffyFrequency();
  const long b = util::GetJiffyFrequency();
  const long c = util::GetJiffyFrequency();
  EXPECT_EQ(a, b);
  EXPECT_EQ(b, c);
}

TEST(GetJiffyFrequencyTest, IsInPlausibleRange) {
  // Real kernels use HZ values of 100, 250, 300, 1000 (and a few exotic
  // ones like 64 or 2000). We accept anything in [1..4000]: this covers
  // every realistic kernel without being so permissive that an arithmetic
  // bug returning, say, 1'000'000 would slip through. Unlike the old
  // ms-per-jiffy helper, HZ=2000 is represented exactly here (no integer-
  // division truncation to 0), which is the point of switching to the
  // frequency form.
  const long hz = util::GetJiffyFrequency();
  EXPECT_GE(hz, 1);
  EXPECT_LE(hz, 4000) << "implausible jiffy frequency " << hz << " Hz";
}

}  // namespace

#endif  // !IS_WINDOWS
