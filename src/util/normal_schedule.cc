#include "normal_schedule.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace util {

std::vector<int> BuildNormalSchedulePoints(const NormalDistribution &dist, int count,
                                           double central_mass, int mean_load, int max_point) {
  if (count <= 0) {
    return {};
  }
  if (mean_load <= 0) {
    return std::vector<int>(static_cast<size_t>(count), 0);
  }

  // Bin masses over [mean - d, mean + d], where [mean - d, mean + d] holds
  // `central_mass` of the distribution.
  const double upper = dist.FindXAxisPositionCDF(central_mass);
  const double lower = dist.GetMean() - (upper - dist.GetMean());
  const double step = (upper - lower) / count;
  std::vector<double> values(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    values[i] = dist.CDF(lower + step * (i + 1)) - dist.CDF(lower + step * i);
  }
  const double mass = std::accumulate(values.begin(), values.end(), 0.0);
  const double total = static_cast<double>(mean_load) * count;
  for (double &v : values) {
    v = (mass > 0.0) ? v / mass * total : static_cast<double>(mean_load);
  }

  // Water-filling: clip at the cap and hand the excess to the points
  // below it, proportionally to their size (keeps the bell shape), until
  // nothing is left over or every point is at the cap.
  if (max_point > 0) {
    const double cap = static_cast<double>(max_point);
    for (int round = 0; round < count; ++round) {
      double excess = 0.0;
      double free_weight = 0.0;
      for (double &v : values) {
        if (v > cap) {
          excess += v - cap;
          v = cap;
        } else if (v < cap) {
          free_weight += v;
        }
      }
      if (excess <= 1e-9 || free_weight <= 0.0) {
        break;
      }
      for (double &v : values) {
        if (v < cap) {
          v += excess * (v / free_weight);
        }
      }
    }
  }

  // Largest-remainder rounding to an exact integer total.
  std::vector<int> points(static_cast<size_t>(count));
  long long assigned = 0;
  for (int i = 0; i < count; ++i) {
    points[i] = static_cast<int>(std::floor(values[i]));
    assigned += points[i];
  }
  long long target_total = std::llround(total);
  if (max_point > 0) {
    target_total = std::min<long long>(target_total, static_cast<long long>(max_point) * count);
  }
  std::vector<int> order(static_cast<size_t>(count));
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    return values[a] - std::floor(values[a]) > values[b] - std::floor(values[b]);
  });
  for (size_t k = 0; assigned < target_total && k < order.size(); ++k) {
    const int i = order[k];
    if (max_point > 0 && points[i] >= max_point) {
      continue;
    }
    ++points[i];
    ++assigned;
  }
  return points;
}

}  // namespace util
