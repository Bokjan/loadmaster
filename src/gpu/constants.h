#pragma once

#include "core/constants.h"

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
constexpr int kGpuKernelLoopBaseMin = 0;
constexpr int kGpuKernelLoopBaseMax = 50'000'000;
constexpr int kGpuKernelLoopBaseTestIteration = 12;

// GPU rand_normal scheduler. Mirrors the CPU rand_normal knobs: every
// device independently walks its own shuffled set of load points so that
// the *time-average* load matches the user-requested `-g <load>` while
// the instantaneous load fluctuates on a normal-distribution-shaped
// envelope. Scheduling cadence is much coarser than CPU's because GPU
// kernel launches are expensive and we don't want to thrash the queue.
constexpr double kGpuRandNormalMu = 0.0;
constexpr double kGpuRandNormalSigma = 2.0;
constexpr double kGpuRandNormalCdfTarget = 0.95;
constexpr int kGpuRandNormalSchedulePeriodMS = 600'000;   // 5min
constexpr int kGpuRandNormalScheduleIntervalMS = 10'000;  // 10s
constexpr int kGpuRandNormalSchedulePointCount =
    kGpuRandNormalSchedulePeriodMS / kGpuRandNormalScheduleIntervalMS;
