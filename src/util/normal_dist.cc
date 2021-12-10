#include "normal_dist.h"

#include <cmath>

namespace util {

template <typename T>
inline T Square(T x) {
  return x * x;
}

NormalDistribution::NormalDistribution() : NormalDistribution(0, 1.0) {}

NormalDistribution::NormalDistribution(double mean, double variance)
    : mean_(mean), stddev_(sqrt(variance)), variance_(variance) {}

double NormalDistribution::PDF(double x) const {
  const static double sqrt_2_pi = sqrt(2.0 * M_PI);
  double t1 = 1.0 / (stddev_ * sqrt_2_pi);
  double t2 = Square(x - mean_) / (2.0 * variance_);
  return t1 * exp(-t2);
}

double NormalDistribution::CDF(double x) const {
  const static double sqrt_2 = sqrt(2.0);
  double t = (x - mean_) / (stddev_ * sqrt_2);
  return 0.5 * (1 + erf(t));
}

}  // namespace util
