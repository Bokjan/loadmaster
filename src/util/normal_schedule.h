#pragma once

#include <vector>

#include "normal_dist.h"

namespace util {

// Build the `count` load points of a rand_normal schedule: the central
// `central_mass` of `dist` is split into `count` equal-width bins and each
// point is proportional to its bin's probability mass, scaled so the points
// average to exactly `mean_load`.
//
// Integer rounding uses the largest-remainder method, so the sum is
// exactly mean_load * count (plain truncation used to lose ~0.5 per point).
//
// With `max_point > 0` no point exceeds it: the excess clipped off the peaks
// is redistributed over the points still below the cap (water-filling), so
// the average is preserved whenever mean_load <= max_point. Without this,
// the GPU schedule (cap 100) averaged only ~76 for `-g 90`.
std::vector<int> BuildNormalSchedulePoints(const NormalDistribution &dist, int count,
                                           double central_mass, int mean_load, int max_point = 0);

}  // namespace util
