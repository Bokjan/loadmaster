#pragma once

#include <cstdint>
#include <random>

namespace gpu {

// The fluctuating factor on a device's memory traffic (see
// kGpuMemTrafficLogSigma): a discretised Ornstein-Uhlenbeck process on the
// factor's logarithm, stepped by the time that actually passed (launches
// skipped while idle, sharing the device or overrunning still count). The log has mean
// -sigma^2 / 2, so the factor itself averages 1 over the long run, and it
// starts from the stationary distribution rather than at the mean. Not
// thread-safe; owned by the device's worker.
class MemTrafficEnvelope {
 public:
  explicit MemTrafficEnvelope(std::uint32_t seed);

  // Step by `elapsed_ms` and return the factor for then, within
  // [kGpuMemTrafficMinFactor, kGpuMemTrafficMaxFactor]. 0 keeps the
  // current value; after many time constants the factor is a fresh draw.
  double Next(double elapsed_ms);

 private:
  std::mt19937 generator_;
  std::normal_distribution<double> noise_;
  double log_factor_;
};

}  // namespace gpu
