#pragma once

#include "core/constants.h"

// CPU module tunables.
constexpr int kCpuSchedulingGranularityNS = 1 * 1000 * 1000;  // 1 ms
constexpr int kCpuMaxLoadPerCore = 100;
constexpr int kCpuBaseLoopCountMin = 0;
constexpr int kCpuBaseLoopCountMax = 25'000'000;
constexpr int kCpuBaseLoopCountTestIteration = 16;
constexpr double kCpuRandNormalMu = 0.0;
constexpr double kCpuRandNormalSigma = 2.0;
constexpr double kCpuRandNormalCdfTarget = 0.95;
constexpr int kCpuRandNormalSchedulePeriodMS = 600'000;   // 5min
constexpr int kCpuRandNormalScheduleIntervalMS = 10'000;  // 10s
constexpr int kCpuRandNormalSchedulePointCount =
    kCpuRandNormalSchedulePeriodMS / kCpuRandNormalScheduleIntervalMS;
constexpr int kCpuAvgLoadSamplingCount = 100;
