#include "util/normal_dist.h"

#include <cmath>

namespace util {

template <typename T>
inline T Square(T x) {
  return x * x;
}

NormalDistribution::NormalDistribution() : NormalDistribution(0, 1.0) {}

NormalDistribution::NormalDistribution(const double mean, const double stddev)
    : mean_(mean), stddev_(stddev), variance_(Square(stddev)) {}

double NormalDistribution::PDF(const double x) const {
  const static double sqrt_2_pi = sqrt(2.0 * M_PI);
  double t1 = 1.0 / (stddev_ * sqrt_2_pi);
  double t2 = Square(x - mean_) / (2.0 * variance_);
  return t1 * exp(-t2);
}

double NormalDistribution::CDF(const double x) const {
  const static double sqrt_2 = sqrt(2.0);
  double t = (x - mean_) / (stddev_ * sqrt_2);
  return 0.5 * (1 + erf(t));
}

double NormalDistribution::FindXAxisPosition(const double target, FnBinSearchCompare compare,
                                             const int max_iteration) const {
  constexpr double kMaxRange = 100000;
  constexpr double kLeftRightEpsilon = 1e-7;
  double l = mean_;
  double r = mean_ + kMaxRange;
  int iteration = 0;
  double x = 0;
  while (l < r && iteration < max_iteration) {
    x = (l + r) / 2;
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
