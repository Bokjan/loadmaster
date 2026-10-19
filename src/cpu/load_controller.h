#pragma once

namespace cpu {

// Turns load measurements into the total CPU load this process should burn
// on the next scheduling tick. Pure (no clocks, no I/O) so it can be
// exercised against a simulated system in unit tests.
//
// Model: system = other + ours. The demand is
//
//   feedforward = target - other_avg          (fill the gap others leave)
//   integral   += kCpuLoadControllerKi * (target - system_avg)
//   demand      = clamp(feedforward + integral, 0, capacity)
//
// The feedforward term reacts to other processes coming and going; the
// small integral term removes the residual bias (workers that under- or
// over-deliver their busy budget, our main thread's own overhead, ...) so
// the system average converges on `target`. The integral is frozen while
// the output is saturated in the direction of the error (anti-windup).
//
// This replaces a deadbeat formula, demand = K*(target - other) - C*procavg
// with K = C + 1 = 101, whose very high gain on noisy averages drove the
// output to the clamp rails ([0, target]) -- an on/off controller that
// spent long stretches at 0 and settled below target.
class LoadController final {
 public:
  // All loads are in "percent of one core" units (a fully busy 4-core
  // machine is 400).
  //   target      : desired system-wide load
  //   system_avg  : recent mean system-wide load, including our own
  //   process_avg : recent mean load of this process
  //   capacity    : the most this process can burn (100 x worker threads)
  // Returns the demand in [0, capacity].
  int Update(int target, double system_avg, double process_avg, int capacity);

  double Integral() const { return integral_; }

 private:
  double integral_ = 0.0;
};

}  // namespace cpu
