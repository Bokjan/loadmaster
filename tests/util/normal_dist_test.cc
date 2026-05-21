// Unit tests for util::NormalDistribution.
//
// The class is a small wrapper around std::erf / std::exp that exposes
// PDF, CDF, and two binary-search helpers used by the random-normal CPU
// scheduler. We test:
//   * default-constructed standard normal matches well-known values
//   * symmetry properties of PDF/CDF
//   * the inverse helpers land on a point whose forward value matches
//     the target within the documented bisection tolerance
//   * binary search converges (and stops) under the iteration cap

#include "util/normal_dist.h"

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

namespace {

using util::NormalDistribution;

// Tolerance for PDF/CDF closed-form comparisons. The implementation
// uses libm's erf/exp directly, so we can afford to be tight.
constexpr double kTight = 1e-12;

// Tolerance for the bisection helpers. FindXAxisPosition stops when
// (r - l) < 1e-7, so the function value at the returned x can drift a
// bit more than that depending on local slope; 1e-5 is comfortable.
constexpr double kBisectionTol = 1e-5;

TEST(NormalDistributionTest, DefaultIsStandardNormal) {
  NormalDistribution n;
  EXPECT_DOUBLE_EQ(n.GetMean(), 0.0);
  EXPECT_DOUBLE_EQ(n.GetStandardDeviation(), 1.0);
}

TEST(NormalDistributionTest, PdfAtMeanMatchesClosedForm) {
  NormalDistribution n;
  // PDF(0) of N(0,1) = 1 / sqrt(2*pi)
  const double expected = 1.0 / std::sqrt(2.0 * std::numbers::pi);
  EXPECT_NEAR(n.PDF(0.0), expected, kTight);
}

TEST(NormalDistributionTest, PdfIsSymmetricAroundMean) {
  NormalDistribution n(5.0, 2.0);
  EXPECT_NEAR(n.PDF(5.0 + 1.5), n.PDF(5.0 - 1.5), kTight);
  EXPECT_NEAR(n.PDF(5.0 + 4.2), n.PDF(5.0 - 4.2), kTight);
}

TEST(NormalDistributionTest, PdfDecreasesAwayFromMean) {
  NormalDistribution n(0.0, 1.0);
  EXPECT_GT(n.PDF(0.0), n.PDF(0.5));
  EXPECT_GT(n.PDF(0.5), n.PDF(1.0));
  EXPECT_GT(n.PDF(1.0), n.PDF(2.0));
  EXPECT_GT(n.PDF(2.0), n.PDF(3.0));
}

TEST(NormalDistributionTest, CdfAtMeanIsHalf) {
  NormalDistribution n(7.0, 3.0);
  EXPECT_NEAR(n.CDF(7.0), 0.5, kTight);
}

TEST(NormalDistributionTest, CdfKnownPoints) {
  // Standard normal: CDF(1) ~= 0.8413447460685429
  //                  CDF(2) ~= 0.9772498680518208
  NormalDistribution n;
  EXPECT_NEAR(n.CDF(1.0), 0.8413447460685429, 1e-12);
  EXPECT_NEAR(n.CDF(2.0), 0.9772498680518208, 1e-12);
  EXPECT_NEAR(n.CDF(-1.0), 1.0 - 0.8413447460685429, 1e-12);
}

TEST(NormalDistributionTest, CdfIsMonotonicallyIncreasing) {
  NormalDistribution n(0.0, 1.5);
  double prev = n.CDF(-5.0);
  for (double x = -4.5; x <= 5.0; x += 0.25) {
    const double cur = n.CDF(x);
    EXPECT_GT(cur, prev) << "at x = " << x;
    prev = cur;
  }
}

TEST(NormalDistributionTest, FindXAxisPositionPdfRoundtrip) {
  // Pick a target PDF value strictly below the peak; the right-hand
  // intersection x is in (mean, +inf). The implementation searches in
  // [mean, mean + 1e5], so any value between 0 and PDF(mean) works.
  NormalDistribution n(0.0, 1.0);
  const double peak = n.PDF(0.0);
  const double target = peak * 0.25;  // somewhere on the right tail

  const double x = n.FindXAxisPositionPDF(target);
  EXPECT_GT(x, n.GetMean()) << "search domain is [mean, mean + 1e5]";
  EXPECT_NEAR(n.PDF(x), target, kBisectionTol);
}

TEST(NormalDistributionTest, FindXAxisPositionCdfRoundtrip) {
  // FindXAxisPositionCDF solves for x s.t. CDF(x) - CDF(2*mean - x) == target,
  // i.e. the probability mass within +/-(x - mean) around the mean.
  NormalDistribution n(0.0, 1.0);
  const double target = 0.6827;  // ~1 sigma mass

  const double x = n.FindXAxisPositionCDF(target);
  EXPECT_GT(x, n.GetMean());

  const double sum = n.CDF(x) - n.CDF(n.GetMean() - (x - n.GetMean()));
  EXPECT_NEAR(sum, target, kBisectionTol);
  // And the x itself should land near 1.0 (the 1-sigma point).
  EXPECT_NEAR(x, 1.0, 1e-3);
}

TEST(NormalDistributionTest, FindXAxisPositionRespectsIterationCap) {
  // Pass a very small max_iteration so the search cannot fully
  // converge. The call must still return without hanging and produce
  // some value inside the search domain.
  NormalDistribution n(0.0, 1.0);
  const double x = n.FindXAxisPositionPDF(0.1, /*max_iteration=*/2);
  // After two halvings of [0, 1e5] the midpoint is bounded.
  EXPECT_GE(x, 0.0);
  EXPECT_LE(x, 1e5);
}

TEST(NormalDistributionTest, NonUnitVarianceScales) {
  // Wider distribution -> lower peak, same symmetry.
  NormalDistribution narrow(0.0, 1.0);
  NormalDistribution wide(0.0, 4.0);
  EXPECT_LT(wide.PDF(0.0), narrow.PDF(0.0));
  // Both still integrate to 0.5 on each side -> CDF(0) == 0.5.
  EXPECT_NEAR(wide.CDF(0.0), 0.5, kTight);
}

}  // namespace
