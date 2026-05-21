// Unit tests for util::AtomicBitmap.
//
// AtomicBitmap is a thin wrapper around std::atomic<T> that exposes
// bitwise Set / Reset / Test / Clear. We don't try to stress-test the
// underlying atomic primitives (that's the standard library's job);
// instead we lock in the bit-level semantics for the typical
// "enum-as-flags" use cases the project relies on.

#include "util/atomic_bitmap.h"

#include <cstdint>

#include <gtest/gtest.h>

namespace {

using util::AtomicBitmap;

TEST(AtomicBitmapTest, DefaultConstructedIsAllZero) {
  AtomicBitmap<uint32_t> bm;
  EXPECT_FALSE(bm.Test(0x1u));
  EXPECT_FALSE(bm.Test(0x2u));
  EXPECT_FALSE(bm.Test(0xFFFFFFFFu));
}

TEST(AtomicBitmapTest, SetAndTestSingleBit) {
  AtomicBitmap<uint32_t> bm;
  bm.Set(0x4u);
  EXPECT_TRUE(bm.Test(0x4u));
  // Adjacent bits must remain untouched.
  EXPECT_FALSE(bm.Test(0x2u));
  EXPECT_FALSE(bm.Test(0x8u));
}

TEST(AtomicBitmapTest, SetIsIdempotent) {
  AtomicBitmap<uint32_t> bm;
  bm.Set(0x10u);
  bm.Set(0x10u);
  bm.Set(0x10u);
  EXPECT_TRUE(bm.Test(0x10u));
}

TEST(AtomicBitmapTest, ResetClearsOnlyTargetBits) {
  AtomicBitmap<uint32_t> bm;
  bm.Set(0x1u);
  bm.Set(0x2u);
  bm.Set(0x4u);
  bm.Reset(0x2u);
  EXPECT_TRUE(bm.Test(0x1u));
  EXPECT_FALSE(bm.Test(0x2u));
  EXPECT_TRUE(bm.Test(0x4u));
}

TEST(AtomicBitmapTest, TestRequiresAllBitsOfMaskToBeSet) {
  // Test() uses `(value & bit) == bit`, i.e. it asks "are *all* of
  // these bits set?" rather than "are any?". This matters when bit is
  // a composite mask.
  AtomicBitmap<uint32_t> bm;
  bm.Set(0x1u);
  EXPECT_FALSE(bm.Test(0x3u))  // 0x1 | 0x2 -- 0x2 missing
      << "Test() should require every bit in the mask to be set";
  bm.Set(0x2u);
  EXPECT_TRUE(bm.Test(0x3u));
}

TEST(AtomicBitmapTest, ClearResetsAllBits) {
  AtomicBitmap<uint32_t> bm;
  bm.Set(0xDEADBEEFu);
  EXPECT_TRUE(bm.Test(0xDEADBEEFu));
  bm.Clear();
  EXPECT_FALSE(bm.Test(0x1u));
  EXPECT_FALSE(bm.Test(0xDEADBEEFu));
}

TEST(AtomicBitmapTest, WorksWithUint64) {
  AtomicBitmap<uint64_t> bm;
  const uint64_t kHighBit = uint64_t{1} << 63;
  bm.Set(kHighBit);
  EXPECT_TRUE(bm.Test(kHighBit));
  EXPECT_FALSE(bm.Test(uint64_t{1}));
  bm.Reset(kHighBit);
  EXPECT_FALSE(bm.Test(kHighBit));
}

TEST(AtomicBitmapTest, ResetOnUnsetBitIsNoop) {
  AtomicBitmap<uint32_t> bm;
  bm.Set(0x1u);
  bm.Reset(0x2u);  // 0x2 was never set; must not affect 0x1.
  EXPECT_TRUE(bm.Test(0x1u));
  EXPECT_FALSE(bm.Test(0x2u));
}

}  // namespace
