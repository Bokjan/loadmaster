// Unit tests for util::FiletimeTo100Ns (Windows-only).
//
// FiletimeTo100Ns assembles a FILETIME's two 32-bit halves into a
// single 64-bit count of 100ns ticks. The function is one line of
// arithmetic but it is the load-bearing helper for
// cpu::stat_windows.cc and util::proc_stat_windows.cc -- a typo
// (high/low swap, signed widening, byte-order assumption) would
// silently corrupt every CPU-busy reading on Windows. The Linux /
// macOS backends have analogous logic exercised by
// tests/cpu/stat_test.cc and util/proc_stat_internal_test.cc; this
// is the Windows counterpart.
//
// The entire TU is guarded by IS_WINDOWS and compiles to zero tests
// on POSIX, matching the convention used by stat_test.cc and
// proc_stat_internal_test.cc.

#include "core/platform.h"

#if IS_WINDOWS

#  include "util/win_util.h"

#  include <cstdint>

#  include <gtest/gtest.h>

namespace {

// Pack high / low 32-bit values into a FILETIME the same way the
// kernel does, so the test reads naturally as "given these two
// halves, expect this combined 64-bit value".
FILETIME MakeFiletime(std::uint32_t high, std::uint32_t low) {
  FILETIME ft{};
  ft.dwLowDateTime = static_cast<DWORD>(low);
  ft.dwHighDateTime = static_cast<DWORD>(high);
  return ft;
}

TEST(FiletimeTo100NsTest, ZeroFiletimeMapsToZero) {
  FILETIME ft = MakeFiletime(0, 0);
  EXPECT_EQ(util::FiletimeTo100Ns(&ft), 0ull);
}

TEST(FiletimeTo100NsTest, LowHalfOnly) {
  // Pure low half: high = 0. The returned value must be exactly the
  // low half, zero-extended.
  FILETIME ft = MakeFiletime(0, 0x1234'5678u);
  EXPECT_EQ(util::FiletimeTo100Ns(&ft), 0x0000'0000'1234'5678ull);
}

TEST(FiletimeTo100NsTest, HighHalfOnly) {
  // Pure high half: must be shifted up by 32 bits, not truncated or
  // sign-extended.
  FILETIME ft = MakeFiletime(0x1234'5678u, 0);
  EXPECT_EQ(util::FiletimeTo100Ns(&ft), 0x1234'5678'0000'0000ull);
}

TEST(FiletimeTo100NsTest, BothHalvesCombine) {
  FILETIME ft = MakeFiletime(0xDEAD'BEEFu, 0xCAFE'BABEu);
  EXPECT_EQ(util::FiletimeTo100Ns(&ft), 0xDEAD'BEEF'CAFE'BABEull);
}

TEST(FiletimeTo100NsTest, MaximumValue) {
  // 0xFFFFFFFF / 0xFFFFFFFF must produce 0xFFFFFFFFFFFFFFFF, not a
  // sign-flip or overflow into 0.
  FILETIME ft = MakeFiletime(0xFFFFFFFFu, 0xFFFFFFFFu);
  EXPECT_EQ(util::FiletimeTo100Ns(&ft), 0xFFFF'FFFF'FFFF'FFFFull);
}

TEST(FiletimeTo100NsTest, HighIsAboveLowNotBelow) {
  // Regression target: an earlier draft accidentally swapped
  // LowPart/HighPart. This case pins the orientation: a high-half
  // value of 1 must produce 2^32, not 1.
  FILETIME ft = MakeFiletime(1, 0);
  EXPECT_EQ(util::FiletimeTo100Ns(&ft), uint64_t{1} << 32);
}

TEST(FiletimeTo100NsTest, MonotonicAcrossSuccessivelyLargerInputs) {
  // Walking the high half up must produce strictly increasing 64-bit
  // values regardless of the low half. Cheap sanity check that the
  // shift count and OR direction are right.
  FILETIME zero = MakeFiletime(0, 0);
  std::uint64_t prev = util::FiletimeTo100Ns(&zero);
  for (std::uint32_t hi : {1u, 2u, 10u, 1000u, 1'000'000u, 0x7FFF'FFFFu, 0xFFFF'FFFFu}) {
    FILETIME ft = MakeFiletime(hi, 0);
    const std::uint64_t cur = util::FiletimeTo100Ns(&ft);
    EXPECT_GT(cur, prev) << "high=" << hi;
    prev = cur;
  }
}

}  // namespace

#endif  // IS_WINDOWS
