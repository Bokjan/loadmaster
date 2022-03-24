#pragma once

#include <functional>

namespace util {

class NormalDistribution final {
 public:
  NormalDistribution();
  NormalDistribution(const double mean, const double stddev);
  double GetMean() const { return mean_; };
  double GetStandardDeviation() const { return stddev_; }
  double PDF(const double x) const;
  double CDF(const double x) const;
  double FindXAxisPositionPDF(const double target, const int max_iteration = 64) const;
  double FindXAxisPositionCDF(const double target, const int max_iteration = 64) const;

 private:
  const double mean_;
  const double stddev_;
  const double variance_;

  using FnBinSearchCompare = std::function<bool(const double x)>;
  double FindXAxisPosition(const double target, FnBinSearchCompare compare,
                           const int max_iteration) const;
};

}  // namespace util
