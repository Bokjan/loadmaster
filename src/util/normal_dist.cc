#include "normal_dist.h"

#include <cmath>

#include <numbers>
#include <numeric>

#include "util/log.h"

namespace util {

template <typename T>
inline T Square(T x) {
  return x * x;
}

NormalDistribution::NormalDistribution() : NormalDistribution(0, 1.0) {}

NormalDistribution::NormalDistribution(const double mean, const double stddev)
    : mean_(mean), stddev_(stddev), variance_(Square(stddev)) {
  // stddev <= 0 makes PDF/CDF divide by zero (1/(stddev*...) and
  // (x-mean)/(stddev*sqrt2)). The distribution is only ever built from the
  // positive k*RandNormalSigma constants, so a non-positive stddev is a
  // programming error -- fail fast rather than hand back inf/NaN downstream.
  if (stddev <= 0.0) {
    LOG_FATAL("NormalDistribution: stddev must be > 0, got %g", stddev);
  }
}

double NormalDistribution::PDF(const double x) const {
  const static double sqrt_2_pi = sqrt(2.0 * std::numbers::pi);
  double t1 = 1.0 / (stddev_ * sqrt_2_pi);
  double t2 = Square(x - mean_) / (2.0 * variance_);
  return t1 * exp(-t2);
}

double NormalDistribution::CDF(const double x) const {
  double t = (x - mean_) / (stddev_ * std::numbers::sqrt2);
  return 0.5 * (1 + erf(t));
}

double NormalDistribution::FindXAxisPosition([[maybe_unused]] const double target,
                                             FnBinSearchCompare compare,
                                             const int max_iteration) const {
  constexpr double kMaxRange = 100'000;
  constexpr double kLeftRightEpsilon = 1e-7;
  double l = mean_;
  double r = mean_ + kMaxRange;
  int iteration = 0;
  double x = 0;
  while (l < r && iteration < max_iteration) {
    x = std::midpoint(l, r);
    if (r - l < kLeftRightEpsilon) {
      break;
    }
    if (compare(x)) {
      r = x;
    } else {
      l = x;
    }
    ++iteration;
  }
  return x;
}

double NormalDistribution::FindXAxisPositionPDF(const double target,
                                                const int max_iteration) const {
  return FindXAxisPosition(
      target,
      [this, target](const double x) -> bool {
        auto y = this->PDF(x);
        return y < target;
      },
      max_iteration);
}

double NormalDistribution::FindXAxisPositionCDF(const double target,
                                                const int max_iteration) const {
  return FindXAxisPosition(
      target,
      [this, target](const double x) -> bool {
        auto sum = this->CDF(x) - this->CDF(x - 2 * (x - this->mean_));
        return sum > target;
      },
      max_iteration);
}

}  // namespace util
