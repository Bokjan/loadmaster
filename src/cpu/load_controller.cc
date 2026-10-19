#include "load_controller.h"

#include <algorithm>
#include <cmath>

#include "constants.h"

namespace cpu {

int LoadController::Update(int target, double system_avg, double process_avg, int capacity) {
  if (capacity <= 0) {
    return 0;
  }
  const double cap = static_cast<double>(capacity);
  const double other = std::max(0.0, system_avg - process_avg);
  const double feedforward = static_cast<double>(target) - other;
  const double error = static_cast<double>(target) - system_avg;

  // Conditional integration: don't wind the integral further while the
  // output is already pinned at the rail the error is pushing towards.
  const double candidate = integral_ + kCpuLoadControllerKi * error;
  const double raw = feedforward + candidate;
  const bool saturated_high = raw > cap && error > 0.0;
  const bool saturated_low = raw < 0.0 && error < 0.0;
  if (!saturated_high && !saturated_low) {
    integral_ = std::clamp(candidate, -cap, cap);
  }

  const double demand = std::clamp(feedforward + integral_, 0.0, cap);
  return static_cast<int>(std::lround(demand));
}

}  // namespace cpu
