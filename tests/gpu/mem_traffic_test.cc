// Unit tests for gpu::MemTrafficEnvelope, the fluctuating factor on a
// device's memory traffic. Statistical checks on long seeded runs: the
// factor averages 1 (so the mean rate holds), stays within its bounds,
// really varies, drifts smoothly instead of jumping every tick, and moves
// by the time that passed rather than by the number of calls.

#include "gpu/mem_traffic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/constants.h"
#include "gpu/constants.h"

#include <gtest/gtest.h>

namespace {

using gpu::MemTrafficEnvelope;

std::vector<double> Walk(std::uint32_t seed, int ticks) {
  MemTrafficEnvelope envelope(seed);
  std::vector<double> out(static_cast<std::size_t>(ticks));
  for (double &f : out) {
    f = envelope.Next(kScheduleIntervalMS);
  }
  return out;
}

// ~14 simulated hours: thousands of time constants.
constexpr int kTicks = 500'000;

TEST(MemTrafficEnvelopeTest, AveragesOneWithinBounds) {
  for (const std::uint32_t seed : {1u, 2u, 3u}) {
    const auto f = Walk(seed, kTicks);
    double sum = 0.0;
    for (double x : f) {
      ASSERT_GE(x, kGpuMemTrafficMinFactor);
      ASSERT_LE(x, kGpuMemTrafficMaxFactor);
      sum += x;
    }
    // The cap trims the upper tail a little.
    EXPECT_NEAR(sum / kTicks, 1.0, 0.04) << "seed " << seed;
  }
}

TEST(MemTrafficEnvelopeTest, Fluctuates) {
  auto f = Walk(7, kTicks);
  std::sort(f.begin(), f.end());
  // Most of the time well below and well above the mean.
  EXPECT_LT(f[f.size() / 10], 0.4) << "10th percentile";
  EXPECT_GT(f[f.size() * 9 / 10], 1.8) << "90th percentile";
}

TEST(MemTrafficEnvelopeTest, DriftsInsteadOfJumping) {
  const auto f = Walk(11, kTicks);
  // Tick to tick the factor moves by a few percent; over the time constant
  // it decorrelates.
  std::vector<double> steps;
  steps.reserve(f.size());
  for (std::size_t i = 1; i < f.size(); ++i) {
    steps.push_back(std::abs(std::log(f[i] / f[i - 1])));
  }
  std::nth_element(steps.begin(), steps.begin() + steps.size() / 2, steps.end());
  EXPECT_LT(steps[steps.size() / 2], 0.15) << "median tick-to-tick change (log)";

  const std::size_t lag = 3 * kGpuMemTrafficTimeConstantMS / kScheduleIntervalMS;
  double same = 0.0;
  for (std::size_t i = lag; i < f.size(); ++i) {
    same += (f[i] > 1.0) == (f[i - lag] > 1.0) ? 1.0 : 0.0;
  }
  EXPECT_LT(same / static_cast<double>(f.size() - lag), 0.65)
      << "still correlated after three time constants";
}

TEST(MemTrafficEnvelopeTest, NoTimeNoChange) {
  MemTrafficEnvelope envelope(3);
  const double first = envelope.Next(0.0);
  EXPECT_EQ(envelope.Next(0.0), first);
  EXPECT_NE(envelope.Next(kScheduleIntervalMS), first);
}

TEST(MemTrafficEnvelopeTest, MovesByElapsedTime) {
  // A pause of many time constants (a device shared for minutes)
  // forgets where the walk was; a single tick doesn't.
  int high_after_tick = 0;
  int high_after_pause = 0;
  constexpr int kRuns = 2'000;
  for (std::uint32_t seed = 0; seed < kRuns; ++seed) {
    MemTrafficEnvelope a(seed);
    MemTrafficEnvelope b(seed);
    const bool high = a.Next(0.0) > 1.0;
    b.Next(0.0);
    high_after_tick += (a.Next(kScheduleIntervalMS) > 1.0) == high ? 1 : 0;
    high_after_pause += (b.Next(100.0 * kGpuMemTrafficTimeConstantMS) > 1.0) == high ? 1 : 0;
  }
  EXPECT_GT(high_after_tick, kRuns * 9 / 10);
  EXPECT_LT(high_after_pause, kRuns * 6 / 10);
}

TEST(MemTrafficEnvelopeTest, SeedsGiveDifferentWalks) {
  EXPECT_NE(Walk(1, 50), Walk(2, 50));
  EXPECT_EQ(Walk(5, 50), Walk(5, 50));
}

}  // namespace
