// Unit tests for util::RollingSampler.
//
// We focus on the boundary conditions that are easy to get wrong in a
// hand-rolled ring buffer:
//   * mean during the warming-up phase (buffer not yet full)
//   * the exact moment we switch from "append" to "overwrite" mode
//   * index wrap-around in steady state
//   * edge cases like capacity == 1

#include "util/rolling_sampler.h"

#include <gtest/gtest.h>

namespace {

using util::RollingSampler;

TEST(RollingSamplerTest, FreshSamplerHasNoSamples) {
  RollingSampler<int> s(4);
  EXPECT_EQ(s.GetSampleCount(), 0);
  // Mean is initialized to T{} before any insert; we don't make
  // promises about its meaningfulness, but it must at least be
  // readable without crashing.
  EXPECT_EQ(s.GetMean(), 0);
}

TEST(RollingSamplerTest, WarmingUpMeanUsesActualSampleCount) {
  // While the buffer is not yet full, the mean should be computed over
  // the number of samples seen so far, not the capacity.
  RollingSampler<int> s(4);
  s.InsertValue(10);
  EXPECT_EQ(s.GetSampleCount(), 1);
  EXPECT_EQ(s.GetMean(), 10);

  s.InsertValue(20);
  EXPECT_EQ(s.GetSampleCount(), 2);
  EXPECT_EQ(s.GetMean(), 15);  // (10 + 20) / 2

  s.InsertValue(30);
  EXPECT_EQ(s.GetSampleCount(), 3);
  EXPECT_EQ(s.GetMean(), 20);  // (10 + 20 + 30) / 3
}

TEST(RollingSamplerTest, MeanAtExactCapacity) {
  RollingSampler<int> s(3);
  s.InsertValue(1);
  s.InsertValue(2);
  s.InsertValue(3);
  EXPECT_EQ(s.GetSampleCount(), 3);
  EXPECT_EQ(s.GetMean(), 2);  // (1+2+3)/3
}

TEST(RollingSamplerTest, OverwritesOldestOnceFull) {
  // After the buffer is full the next insert must evict the *oldest*
  // sample (which is values_[0] on the first wrap).
  RollingSampler<int> s(3);
  s.InsertValue(1);
  s.InsertValue(2);
  s.InsertValue(3);
  s.InsertValue(4);  // evicts the original 1

  // Capacity must not grow.
  EXPECT_EQ(s.GetSampleCount(), 3);
  // Buffer now logically holds {2, 3, 4}.
  EXPECT_EQ(s.GetMean(), 3);  // (2+3+4)/3
}

TEST(RollingSamplerTest, FullRingRotation) {
  // Push enough values to wrap the ring index multiple times and
  // verify the running mean stays correct.
  RollingSampler<int> s(4);
  for (int v : {1, 2, 3, 4}) {
    s.InsertValue(v);
  }
  EXPECT_EQ(s.GetMean(), 2);  // (1+2+3+4)/4 = 2 (integer division: 10/4)

  // Now overwrite each slot once more.
  s.InsertValue(5);   // evicts 1 -> {5,2,3,4}, mean = 14/4 = 3
  EXPECT_EQ(s.GetMean(), 3);
  s.InsertValue(6);   // evicts 2 -> {5,6,3,4}, mean = 18/4 = 4
  EXPECT_EQ(s.GetMean(), 4);
  s.InsertValue(7);   // evicts 3 -> {5,6,7,4}, mean = 22/4 = 5
  EXPECT_EQ(s.GetMean(), 5);
  s.InsertValue(8);   // evicts 4 -> {5,6,7,8}, mean = 26/4 = 6
  EXPECT_EQ(s.GetMean(), 6);

  // Second full rotation -- exercises the index wrap-around branch
  // (index_ >= capacity -> index_ = 0).
  s.InsertValue(9);   // evicts 5 -> {9,6,7,8}, mean = 30/4 = 7
  EXPECT_EQ(s.GetMean(), 7);
}

TEST(RollingSamplerTest, CapacityOneAlwaysHoldsLatest) {
  RollingSampler<int> s(1);
  s.InsertValue(42);
  EXPECT_EQ(s.GetSampleCount(), 1);
  EXPECT_EQ(s.GetMean(), 42);

  s.InsertValue(7);
  EXPECT_EQ(s.GetSampleCount(), 1);
  EXPECT_EQ(s.GetMean(), 7);

  s.InsertValue(-100);
  EXPECT_EQ(s.GetSampleCount(), 1);
  EXPECT_EQ(s.GetMean(), -100);
}

TEST(RollingSamplerTest, WorksWithDoubles) {
  RollingSampler<double> s(3);
  s.InsertValue(1.0);
  s.InsertValue(2.0);
  s.InsertValue(3.0);
  EXPECT_DOUBLE_EQ(s.GetMean(), 2.0);

  s.InsertValue(4.0);  // {2,3,4}
  EXPECT_DOUBLE_EQ(s.GetMean(), 3.0);
}

TEST(RollingSamplerTest, HandlesNegativeValues) {
  RollingSampler<int> s(3);
  s.InsertValue(-3);
  s.InsertValue(-6);
  s.InsertValue(-9);
  EXPECT_EQ(s.GetMean(), -6);
}

}  // namespace
