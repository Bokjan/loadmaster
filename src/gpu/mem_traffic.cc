#include "mem_traffic.h"

#include <algorithm>
#include <cmath>

#include "constants.h"

namespace gpu {

namespace {

constexpr double kLogMean = -kGpuMemTrafficLogSigma * kGpuMemTrafficLogSigma / 2.0;

}  // namespace

MemTrafficEnvelope::MemTrafficEnvelope(std::uint32_t seed)
    : generator_(seed), noise_(0.0, 1.0), log_factor_(0.0) {
  log_factor_ = kLogMean + kGpuMemTrafficLogSigma * noise_(generator_);
}

double MemTrafficEnvelope::Next(double elapsed_ms) {
  // Exact discretisation: decay towards the mean by exp(-dt / tau) and add
  // the noise that keeps the stationary deviation at sigma.
  if (elapsed_ms > 0.0) {
    const double decay = std::exp(-elapsed_ms / static_cast<double>(kGpuMemTrafficTimeConstantMS));
    const double step_sigma = kGpuMemTrafficLogSigma * std::sqrt(1.0 - decay * decay);
    log_factor_ = kLogMean + (log_factor_ - kLogMean) * decay + step_sigma * noise_(generator_);
  }
  return std::clamp(std::exp(log_factor_), kGpuMemTrafficMinFactor, kGpuMemTrafficMaxFactor);
}

}  // namespace gpu
