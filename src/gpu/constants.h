#pragma once

// GPU module tunables.
//
// Per-device load is in the range [0, 100], expressed as the fraction of
// each `kScheduleIntervalMS` tick spent submitting / waiting on the busy
// kernel. The kernel itself does pure ALU work (32-bit integer madd
// chains) and is launched with a fixed grid size; we tune the per-thread
// loop count once at startup so a "100% busy" tick equals one full
// scheduling period.
constexpr int kGpuMaxLoadPerDevice = 100;
constexpr int kGpuKernelGridSize = 1024;
constexpr int kGpuKernelBlockSize = 256;
// Upper bound for the per-thread loop count. Generous: on a top-end GPU
// the whole grid runs in a single wave, so one scheduling period
// (100 ms) can take several hundred million iterations per thread.
constexpr int kGpuKernelLoopBaseMax = 1'000'000'000;
// Calibration: a sample must take at least this long to be timed
// reliably; the estimate is accepted once within this relative error.
constexpr double kGpuCalibrationMinSampleMs = 5.0;
constexpr double kGpuCalibrationTolerance = 0.03;
constexpr int kGpuCalibrationRefineRounds = 3;
// Runtime drift correction (clock boost / throttle): only ticks with at
// least this much busy time are used, drift below the deadband is ignored.
constexpr int kGpuAdaptMinBusyMs = 10;
constexpr double kGpuAdaptDeadband = 0.05;
constexpr double kGpuAdaptSmoothing = 0.5;
// After this many consecutive failed launches (~5 s of retries) the
// worker gives the device up instead of logging a warning forever.
constexpr int kGpuMaxConsecutiveFailures = 50;

// GPU rand_normal scheduler. Mirrors the CPU rand_normal knobs: every
// device independently walks its own shuffled set of load points so that
// the *time-average* load matches the user-requested `-g <load>` while
// the instantaneous load fluctuates on a normal-distribution-shaped
// envelope. Scheduling cadence is much coarser than CPU's because GPU
// kernel launches are expensive and we don't want to thrash the queue.
constexpr double kGpuRandNormalMu = 0.0;
constexpr double kGpuRandNormalSigma = 2.0;
constexpr double kGpuRandNormalCdfTarget = 0.95;
constexpr int kGpuRandNormalSchedulePeriodMS = 600'000;   // 10min
constexpr int kGpuRandNormalScheduleIntervalMS = 10'000;  // 10s
constexpr int kGpuRandNormalSchedulePointCount =
    kGpuRandNormalSchedulePeriodMS / kGpuRandNormalScheduleIntervalMS;
