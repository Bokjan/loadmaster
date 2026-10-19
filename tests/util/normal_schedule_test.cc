// Unit tests for util::BuildNormalSchedulePoints, the schedule behind both
// rand_normal algorithms (`-ca rand_normal`, `-ga rand_normal`).
//
// The contract the README promises is "the average over one period equals
// the requested load". We pin it exactly (integer sum == load * count) for
// the uncapped CPU flavour and for the GPU flavour capped at 100, including
// loads where the peaks used to be clipped away (the GPU average for -g 90
// was ~76 before water-filling). We also pin the bell shape: peak in the
// middle, symmetric, and the CPU peak/trough ratio the worker-pool sizing
// relies on.

#include "util/normal_schedule.h"

#include <algorithm>
#include <numeric>
#include <vector>

#include "util/normal_dist.h"

#include <gtest/gtest.h>

namespace {

using util::BuildNormalSchedulePoints;
using util::NormalDistribution;

constexpr int kCount = 60;
constexpr double kMass = 0.95;

long long Sum(const std::vector<int> &v) { return std::accumulate(v.begin(), v.end(), 0LL); }

TEST(NormalScheduleTest, AverageIsExactWithoutCap) {
  const NormalDistribution dist(0.0, 2.0);
  for (int load : {1, 37, 100, 200, 350, 1234}) {
    const auto points = BuildNormalSchedulePoints(dist, kCount, kMass, load);
    ASSERT_EQ(points.size(), static_cast<size_t>(kCount));
    EXPECT_EQ(Sum(points), static_cast<long long>(load) * kCount) << "load=" << load;
  }
}

TEST(NormalScheduleTest, BellShapeWithoutCap) {
  const NormalDistribution dist(0.0, 2.0);
  const auto points = BuildNormalSchedulePoints(dist, kCount, kMass, 200);
  const auto peak = std::max_element(points.begin(), points.end());
  const auto idx = std::distance(points.begin(), peak);
  EXPECT_TRUE(idx == kCount / 2 - 1 || idx == kCount / 2) << "peak at " << idx;
  // Roughly symmetric (rounding may differ by one unit).
  for (int i = 0; i < kCount / 2; ++i) {
    EXPECT_NEAR(points[i], points[kCount - 1 - i], 1) << "i=" << i;
  }
  // Peak ~1.65x and trough ~0.24x of the average (+-1.96 sigma window).
  EXPECT_NEAR(*peak, 330, 10);
  EXPECT_NEAR(*std::min_element(points.begin(), points.end()), 48, 5);
}

TEST(NormalScheduleTest, CappedAverageIsPreserved) {
  const NormalDistribution dist(0.0, 2.0);
  for (int load : {10, 50, 60, 70, 80, 90, 99, 100}) {
    const auto points = BuildNormalSchedulePoints(dist, kCount, kMass, load, 100);
    EXPECT_EQ(Sum(points), static_cast<long long>(load) * kCount) << "load=" << load;
    for (int p : points) {
      EXPECT_GE(p, 0);
      EXPECT_LE(p, 100) << "load=" << load;
    }
  }
}

TEST(NormalScheduleTest, FullLoadIsFlat) {
  const NormalDistribution dist(0.0, 2.0);
  const auto points = BuildNormalSchedulePoints(dist, kCount, kMass, 100, 100);
  for (int p : points) {
    EXPECT_EQ(p, 100);
  }
}

TEST(NormalScheduleTest, LowLoadIsUnaffectedByCap) {
  // Peak ~1.65 x 40 = 66 < 100: capping must not change anything.
  const NormalDistribution dist(0.0, 2.0);
  EXPECT_EQ(BuildNormalSchedulePoints(dist, kCount, kMass, 40, 100),
            BuildNormalSchedulePoints(dist, kCount, kMass, 40));
}

TEST(NormalScheduleTest, DegenerateInputs) {
  const NormalDistribution dist(0.0, 2.0);
  EXPECT_TRUE(BuildNormalSchedulePoints(dist, 0, kMass, 100).empty());
  const auto zeros = BuildNormalSchedulePoints(dist, kCount, kMass, 0);
  EXPECT_EQ(Sum(zeros), 0);
}

}  // namespace
