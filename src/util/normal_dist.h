#pragma once

namespace util {

class NormalDistribution final {
 public:
  NormalDistribution();
  NormalDistribution(double mean, double variance);
  double PDF(double x) const;
  double CDF(double x) const;

 private:
  const double mean_;
  const double stddev_;
  const double variance_;
};

}  // namespace util
